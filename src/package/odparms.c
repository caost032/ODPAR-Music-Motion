#include "odm_package.h"
#include "odm_music_map.h"
#include "package_internal.h"
#include "ir_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define PKG_MANIFEST_NAME "manifest.odm"
#define PKG_SIGNATURE_NAME "signature.ed25519"
#define PKG_RENDER_NAME "render.ir"
#define PKG_ANALYSIS_NAME "analysis.bin"
#define PKG_LOCAL_BYTES UINT32_C(30)
#define PKG_CENTRAL_BYTES UINT32_C(46)
#define PKG_EOCD_BYTES UINT32_C(22)
#define PKG_ZIP_VERSION UINT16_C(20)
#define PKG_MAX_ALL_ENTRIES (ODM_ODPARMS_MAX_ENTRIES + UINT32_C(2))

static const uint8_t PKG_MANIFEST_MAGIC[8] = {
    (uint8_t)'O',(uint8_t)'D',(uint8_t)'P',(uint8_t)'A',
    (uint8_t)'R',(uint8_t)'M',(uint8_t)'S',0u
};

typedef struct {
    uint32_t kind;
    uint32_t method;
    uint32_t flags;
    uint32_t resource_id;
    const char *path;
    uint32_t path_len;
    const uint8_t *data;
    uint64_t data_bytes;
    uint8_t *compressed;
    uint64_t compressed_bytes;
    uint32_t crc32;
    odm_sha256_digest sha256;
    uint64_t local_offset;
} pkg_entry;

typedef struct {
    const uint8_t *package;
    uint64_t package_size;
    const uint8_t *manifest;
    uint64_t manifest_bytes;
    const uint8_t *signature;
    uint8_t public_key[32];
    uint32_t manifest_entry_count;
    pkg_entry *entries; /* manifest-listed entries only */
    uint64_t total_uncompressed;
    odm_sha256_digest score_sha;
    odm_sha256_digest capability_sha;
    odm_sha256_digest music_map_sha;
    odm_sha256_digest music_policy_sha;
    odm_sha256_digest render_ir_sha;
} pkg_verified;

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4u) << 32u);
}
static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & UINT16_C(0xff)); p[1] = (uint8_t)(v >> 8u);
}
static void wr32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8u); p[2]=(uint8_t)(v>>16u); p[3]=(uint8_t)(v>>24u);
}
static void wr64(uint8_t *p, uint64_t v) {
    wr32(p,(uint32_t)(v & UINT64_C(0xffffffff))); wr32(p+4u,(uint32_t)(v>>32u));
}

static int digest_zero(const odm_sha256_digest *d) {
    uint32_t i; uint8_t x=0u;
    for (i=0u;i<ODM_SHA256_DIGEST_BYTES;i++) x=(uint8_t)(x|d->bytes[i]);
    return x==0u;
}
static int bytes_equal_ct(const uint8_t *a,const uint8_t *b,uint32_t n) {
    uint32_t i; uint8_t x=0u;
    for(i=0u;i<n;i++) x=(uint8_t)(x|(uint8_t)(a[i]^b[i]));
    return x==0u;
}
static int add_u64(uint64_t a,uint64_t b,uint64_t *out) {
    if (a > UINT64_MAX - b) return 0;
    *out = a + b;
    return 1;
}
static int fits_u32(uint64_t v) { return v<=UINT32_MAX; }
static int u64_fits_size(uint64_t v) {
#if SIZE_MAX < UINT64_MAX
    return v <= (uint64_t)SIZE_MAX;
#else
    (void)v; return 1;
#endif
}

static int path_valid(const char *path,uint32_t *out_len) {
    uint32_t n=0u, seg=0u; int seg_dot=1, seg_dotdot=1;
    if (!path || path[0]=='\0' || path[0]=='/') return 0;
    while (path[n]!='\0') {
        unsigned char c=(unsigned char)path[n];
        if (n>=ODM_ODPARMS_MAX_PATH_BYTES) return 0;
        if (!((c>=(unsigned char)'a'&&c<=(unsigned char)'z') ||
              (c>=(unsigned char)'0'&&c<=(unsigned char)'9') ||
              c==(unsigned char)'.'||c==(unsigned char)'_'||c==(unsigned char)'-'||c==(unsigned char)'/')) return 0;
        if (c==(unsigned char)'/') {
            if (seg==0u || seg_dot || seg_dotdot) return 0;
            seg=0u; seg_dot=1; seg_dotdot=1;
        } else {
            if (seg==0u) { seg_dot=(c==(unsigned char)'.'); seg_dotdot=(c==(unsigned char)'.'); }
            else if (seg==1u) { seg_dot=0; seg_dotdot=seg_dotdot && (c==(unsigned char)'.'); }
            else { seg_dot=0; seg_dotdot=0; }
            seg++;
        }
        n++;
    }
    if (seg==0u || seg_dot || seg_dotdot) return 0;
    if (out_len) *out_len=n;
    return 1;
}
static int reserved_path(const char *p) {
    return strcmp(p,PKG_MANIFEST_NAME)==0 || strcmp(p,PKG_SIGNATURE_NAME)==0 ||
           strcmp(p,PKG_RENDER_NAME)==0 || strcmp(p,PKG_ANALYSIS_NAME)==0;
}
static int entry_cmp(const void *aa,const void *bb) {
    const pkg_entry *a=(const pkg_entry *)aa,*b=(const pkg_entry *)bb;
    return strcmp(a->path,b->path);
}
static void free_entries(pkg_entry *e,uint32_t n) {
    uint32_t i; if (!e) return;
    for(i=0u;i<n;i++) free(e[i].compressed);
    free(e);
}

static odm_status sha_data(const uint8_t *p,uint64_t n,odm_sha256_digest *d) {
    if (!d || (!p && n!=0u)) return ODM_STATUS_INVALID_ARGUMENT;
    return odm_sha256(p,n,d);
}

static odm_status validate_score_assets(const odm_score_view *score,
                                        const odm_odparms_asset *assets,uint32_t asset_count,
                                        pkg_entry *out_assets) {
    uint32_t i,j;
    if (asset_count>ODM_ODPARMS_MAX_ENTRIES-2u) return ODM_STATUS_BUDGET_EXCEEDED;
    for(i=0u;i<asset_count;i++) {
        uint32_t plen=0u; odm_sha256_digest d; odm_status st;
        const odm_odparms_asset *a=&assets[i];
        if ((a->data==NULL&&a->data_bytes!=0u)||!path_valid(a->path,&plen)||reserved_path(a->path) ||
            a->reserved!=0u || a->flags!=0u || a->data_bytes>ODM_ODPARMS_MAX_ENTRY_BYTES ||
            (a->kind!=ODM_ODPARMS_ENTRY_MEDIA&&a->kind!=ODM_ODPARMS_ENTRY_DATA)) return ODM_STATUS_INVALID_DATA;
        if (a->kind==ODM_ODPARMS_ENTRY_DATA && a->resource_id!=0u) return ODM_STATUS_INVALID_DATA;
        if (a->kind==ODM_ODPARMS_ENTRY_MEDIA && a->resource_id==0u) return ODM_STATUS_INVALID_DATA;
        for(j=0u;j<i;j++) if(strcmp(a->path,assets[j].path)==0) return ODM_STATUS_INVALID_DATA;
        st=sha_data(a->data,a->data_bytes,&d); if(st!=ODM_STATUS_OK)return st;
        out_assets[i].kind=a->kind; out_assets[i].method=(a->kind==ODM_ODPARMS_ENTRY_DATA)?ODM_ODPARMS_METHOD_DEFLATE:ODM_ODPARMS_METHOD_STORE;
        out_assets[i].flags=0u; out_assets[i].resource_id=a->resource_id; out_assets[i].path=a->path; out_assets[i].path_len=plen;
        out_assets[i].data=a->data; out_assets[i].data_bytes=a->data_bytes; out_assets[i].compressed=NULL; out_assets[i].compressed_bytes=a->data_bytes; out_assets[i].sha256=d;
        st=odm_pkg_crc32(a->data,a->data_bytes,&out_assets[i].crc32); if(st!=ODM_STATUS_OK)return st;
    }
    if (score->header->resource_count>asset_count) return ODM_STATUS_INVALID_DATA;
    for(i=0u;i<score->header->resource_count;i++) {
        uint32_t found=0u;
        for(j=0u;j<asset_count;j++) if(assets[j].kind==ODM_ODPARMS_ENTRY_MEDIA && assets[j].resource_id==score->resources[i].resource_id) {
            if(++found>1u) return ODM_STATUS_INVALID_DATA;
            if(!odm_sha256_equal(&out_assets[j].sha256,&score->resources[i].content_sha256)) return ODM_STATUS_INTEGRITY_ERROR;
        }
        if(found!=1u) return ODM_STATUS_INVALID_DATA;
    }
    for(i=0u;i<asset_count;i++) if(assets[i].kind==ODM_ODPARMS_ENTRY_MEDIA) {
        uint32_t found=0u; for(j=0u;j<score->header->resource_count;j++) if(assets[i].resource_id==score->resources[j].resource_id) found++;
        if(found!=1u) return ODM_STATUS_INVALID_DATA;
    }
    return ODM_STATUS_OK;
}

static void manifest_write_entry(uint8_t *p,const pkg_entry *e) {
    memset(p,0,ODM_ODPARMS_MANIFEST_ENTRY_BYTES);
    wr32(p+0u,e->kind); wr32(p+4u,e->method); wr32(p+8u,e->flags); wr32(p+12u,e->resource_id);
    wr64(p+16u,e->data_bytes); memcpy(p+24u,e->sha256.bytes,32u); wr32(p+56u,e->path_len);
    memcpy(p+64u,e->path,e->path_len);
}
static odm_status manifest_build(const odm_sha256_digest *score_sha,const odm_sha256_digest *cap_sha,
                                 const odm_sha256_digest *map_sha,const odm_sha256_digest *policy_sha,
                                 const odm_sha256_digest *ir_sha,const uint8_t pub[32],
                                 const pkg_entry *entries,uint32_t n,uint64_t total,
                                 uint8_t **out,uint64_t *out_bytes) {
    uint64_t bytes,i; uint8_t *m;
    if(n>ODM_ODPARMS_MAX_ENTRIES) return ODM_STATUS_BUDGET_EXCEEDED;
    if (!add_u64(ODM_ODPARMS_MANIFEST_HEADER_BYTES,(uint64_t)n*ODM_ODPARMS_MANIFEST_ENTRY_BYTES,&bytes) || !u64_fits_size(bytes)) return ODM_STATUS_OVERFLOW;
    m=(uint8_t*)malloc((size_t)bytes); if(!m)return ODM_STATUS_OUT_OF_MEMORY; memset(m,0,(size_t)bytes);
    memcpy(m,PKG_MANIFEST_MAGIC,8u); wr32(m+8u,ODM_ODPARMS_SCHEMA_MAJOR); wr32(m+12u,ODM_ODPARMS_SCHEMA_MINOR); wr32(m+16u,0u); wr32(m+20u,n); wr64(m+24u,total);
    memcpy(m+32u,score_sha->bytes,32u); memcpy(m+64u,cap_sha->bytes,32u); memcpy(m+96u,map_sha->bytes,32u); memcpy(m+128u,policy_sha->bytes,32u); memcpy(m+160u,ir_sha->bytes,32u); memcpy(m+192u,pub,32u);
    wr32(m+224u,ODM_ODPARMS_SIGNATURE_ED25519); wr32(m+228u,ODM_ODPARMS_MANIFEST_ENTRY_BYTES);
    for(i=0u;i<n;i++) manifest_write_entry(m+ODM_ODPARMS_MANIFEST_HEADER_BYTES+i*ODM_ODPARMS_MANIFEST_ENTRY_BYTES,&entries[i]);
    *out=m; *out_bytes=bytes; return ODM_STATUS_OK;
}

static void zip_local(uint8_t *p,const pkg_entry *e) {
    wr32(p,UINT32_C(0x04034b50)); wr16(p+4u,PKG_ZIP_VERSION); wr16(p+6u,0u); wr16(p+8u,(uint16_t)e->method); wr16(p+10u,0u); wr16(p+12u,0u);
    wr32(p+14u,e->crc32); wr32(p+18u,(uint32_t)e->compressed_bytes); wr32(p+22u,(uint32_t)e->data_bytes); wr16(p+26u,(uint16_t)e->path_len); wr16(p+28u,0u);
}
static void zip_central(uint8_t *p,const pkg_entry *e) {
    wr32(p,UINT32_C(0x02014b50)); wr16(p+4u,PKG_ZIP_VERSION); wr16(p+6u,PKG_ZIP_VERSION); wr16(p+8u,0u); wr16(p+10u,(uint16_t)e->method); wr16(p+12u,0u); wr16(p+14u,0u);
    wr32(p+16u,e->crc32); wr32(p+20u,(uint32_t)e->compressed_bytes); wr32(p+24u,(uint32_t)e->data_bytes); wr16(p+28u,(uint16_t)e->path_len); wr16(p+30u,0u); wr16(p+32u,0u); wr16(p+34u,0u); wr16(p+36u,0u); wr32(p+38u,0u); wr32(p+42u,(uint32_t)e->local_offset);
}

static odm_status prepare_entry(pkg_entry *e) {
    odm_status st;
    if(!fits_u32(e->data_bytes)) return ODM_STATUS_BUDGET_EXCEEDED;
    st=odm_pkg_crc32(e->data,e->data_bytes,&e->crc32); if(st!=ODM_STATUS_OK)return st;
    st=sha_data(e->data,e->data_bytes,&e->sha256); if(st!=ODM_STATUS_OK)return st;
    if(e->method==ODM_ODPARMS_METHOD_STORE){e->compressed_bytes=e->data_bytes;return ODM_STATUS_OK;}
    st=odm_pkg_deflate_fixed(e->data,e->data_bytes,NULL,0u,&e->compressed_bytes); if(st!=ODM_STATUS_OK)return st;
    if(!fits_u32(e->compressed_bytes)||!u64_fits_size(e->compressed_bytes))return ODM_STATUS_BUDGET_EXCEEDED;
    if(e->compressed_bytes!=0u){e->compressed=(uint8_t*)malloc((size_t)e->compressed_bytes);if(!e->compressed)return ODM_STATUS_OUT_OF_MEMORY;}
    return odm_pkg_deflate_fixed(e->data,e->data_bytes,e->compressed,e->compressed_bytes,&e->compressed_bytes);
}

odm_status odm_odparms_build(const odm_score_view *score,const uint8_t *render_ir,uint64_t render_ir_bytes,
    const uint8_t *analysis_bin,uint64_t analysis_bin_bytes,const odm_odparms_asset *assets,uint32_t asset_count,
    const uint8_t signer_seed[32],uint8_t *buffer,uint64_t capacity,uint64_t *out_required,odm_odparms_info *out_info) {
    odm_sha256_digest score_sha,cap_sha,policy_sha,ir_sha,map_sha,manifest_sha,package_sha;
    odm_render_ir_info iri; odm_music_map_info mi; uint64_t required_ticks=0u;
    pkg_entry *listed=NULL,*all=NULL; uint32_t listed_n=2u+asset_count, all_n=listed_n+2u, i; uint64_t total=0u,manifest_bytes=0u,need=0u,central_off,central_bytes;
    uint8_t *manifest=NULL,signature[64],pub[32]; odm_status st; uint64_t off;
    odm_odparms_info info;
    if(!score||!score->header||!render_ir||!analysis_bin||(!assets&&asset_count)||!signer_seed||!out_required||!out_info) return ODM_STATUS_INVALID_ARGUMENT;
    if(asset_count>ODM_ODPARMS_MAX_ENTRIES-2u) return ODM_STATUS_BUDGET_EXCEEDED;
    st=odm_score_validate(score);if(st!=ODM_STATUS_OK)return st; st=odm_score_hash(score,&score_sha);if(st!=ODM_STATUS_OK)return st; st=odm_score_capability_hash(score,&cap_sha);if(st!=ODM_STATUS_OK)return st;
    st=odm_music_policy_current_sha256(&policy_sha);if(st!=ODM_STATUS_OK)return st;
    st=odm_render_ir_validate(render_ir,render_ir_bytes,&score_sha,&policy_sha,&iri);if(st!=ODM_STATUS_OK)return st;
    st=odm_music_map_record_validate(analysis_bin,analysis_bin_bytes,&policy_sha,&mi,&required_ticks);if(st!=ODM_STATUS_OK)return st;
    (void)required_ticks;
    if(iri.canonical_music_frames!=mi.canonical_frames || !odm_sha256_equal(&iri.music_map_sha256,&mi.record_sha256) || !odm_sha256_equal(&iri.capability_sha256,&cap_sha)) return ODM_STATUS_INTEGRITY_ERROR;
    ir_sha=iri.record_sha256; map_sha=mi.record_sha256;
    if(render_ir_bytes>ODM_ODPARMS_MAX_ENTRY_BYTES||analysis_bin_bytes>ODM_ODPARMS_MAX_ENTRY_BYTES||!fits_u32(render_ir_bytes)||!fits_u32(analysis_bin_bytes))return ODM_STATUS_BUDGET_EXCEEDED;
    listed=(pkg_entry*)calloc(listed_n,sizeof(*listed)); all=(pkg_entry*)calloc(all_n,sizeof(*all)); if(!listed||!all){free(listed);free(all);return ODM_STATUS_OUT_OF_MEMORY;}
    listed[0].kind=ODM_ODPARMS_ENTRY_RENDER_IR; listed[0].method=ODM_ODPARMS_METHOD_STORE; listed[0].path=PKG_RENDER_NAME; listed[0].path_len=(uint32_t)strlen(PKG_RENDER_NAME); listed[0].data=render_ir; listed[0].data_bytes=render_ir_bytes;
    listed[1].kind=ODM_ODPARMS_ENTRY_ANALYSIS; listed[1].method=ODM_ODPARMS_METHOD_STORE; listed[1].path=PKG_ANALYSIS_NAME; listed[1].path_len=(uint32_t)strlen(PKG_ANALYSIS_NAME); listed[1].data=analysis_bin; listed[1].data_bytes=analysis_bin_bytes;
    if(asset_count){st=validate_score_assets(score,assets,asset_count,listed+2u);if(st!=ODM_STATUS_OK)goto fail; qsort(listed+2u,asset_count,sizeof(*listed),entry_cmp);}
    for(i=0u;i<listed_n;i++){ if(i>0u&&strcmp(listed[i-1u].path,listed[i].path)==0){st=ODM_STATUS_INVALID_DATA;goto fail;} st=prepare_entry(&listed[i]);if(st!=ODM_STATUS_OK)goto fail; if(!add_u64(total,listed[i].data_bytes,&total)||total>ODM_ODPARMS_MAX_TOTAL_UNCOMPRESSED){st=ODM_STATUS_BUDGET_EXCEEDED;goto fail;} }
    st=odm_pkg_ed25519_public_from_seed(signer_seed,pub);if(st!=ODM_STATUS_OK)goto fail;
    st=manifest_build(&score_sha,&cap_sha,&map_sha,&policy_sha,&ir_sha,pub,listed,listed_n,total,&manifest,&manifest_bytes);if(st!=ODM_STATUS_OK)goto fail;
    st=odm_pkg_ed25519_sign(signer_seed,manifest,manifest_bytes,signature);if(st!=ODM_STATUS_OK)goto fail;
    memset(all,0,(size_t)all_n*sizeof(*all));
    all[0].kind=0u;all[0].method=0u;all[0].path=PKG_MANIFEST_NAME;all[0].path_len=(uint32_t)strlen(PKG_MANIFEST_NAME);all[0].data=manifest;all[0].data_bytes=manifest_bytes;
    all[1].kind=0u;all[1].method=0u;all[1].path=PKG_SIGNATURE_NAME;all[1].path_len=(uint32_t)strlen(PKG_SIGNATURE_NAME);all[1].data=signature;all[1].data_bytes=64u;
    for(i=0u;i<listed_n;i++) all[i+2u]=listed[i];
    for(i=0u;i<all_n;i++){
        if (i < 2u) { st=prepare_entry(&all[i]); if(st!=ODM_STATUS_OK)goto fail; }
        if(!fits_u32(all[i].compressed_bytes)){st=ODM_STATUS_BUDGET_EXCEEDED;goto fail;} all[i].local_offset=need; if(!fits_u32(need)||!add_u64(need,PKG_LOCAL_BYTES+all[i].path_len,&need)||!add_u64(need,all[i].compressed_bytes,&need)){st=ODM_STATUS_OVERFLOW;goto fail;}}
    central_off=need; central_bytes=0u; for(i=0u;i<all_n;i++) if(!add_u64(central_bytes,PKG_CENTRAL_BYTES+all[i].path_len,&central_bytes)){st=ODM_STATUS_OVERFLOW;goto fail;}
    if(!fits_u32(central_off)||!fits_u32(central_bytes)||all_n>UINT16_MAX||!add_u64(need,central_bytes,&need)||!add_u64(need,PKG_EOCD_BYTES,&need)||need>ODM_ODPARMS_MAX_PACKAGE_BYTES){st=ODM_STATUS_BUDGET_EXCEEDED;goto fail;}
    *out_required=need; if(!buffer||capacity<need){st=ODM_STATUS_BUFFER_TOO_SMALL;goto fail;}
    off=0u; for(i=0u;i<all_n;i++){zip_local(buffer+off,&all[i]);off+=PKG_LOCAL_BYTES;memcpy(buffer+off,all[i].path,all[i].path_len);off+=all[i].path_len;if(all[i].method==0u)memcpy(buffer+off,all[i].data,(size_t)all[i].data_bytes);else if(all[i].compressed_bytes)memcpy(buffer+off,all[i].compressed,(size_t)all[i].compressed_bytes);off+=all[i].compressed_bytes;}
    if(off!=central_off){st=ODM_STATUS_INVARIANT_BROKEN;goto fail;}
    for(i=0u;i<all_n;i++){zip_central(buffer+off,&all[i]);off+=PKG_CENTRAL_BYTES;memcpy(buffer+off,all[i].path,all[i].path_len);off+=all[i].path_len;}
    wr32(buffer+off,UINT32_C(0x06054b50));wr16(buffer+off+4u,0u);wr16(buffer+off+6u,0u);wr16(buffer+off+8u,(uint16_t)all_n);wr16(buffer+off+10u,(uint16_t)all_n);wr32(buffer+off+12u,(uint32_t)central_bytes);wr32(buffer+off+16u,(uint32_t)central_off);wr16(buffer+off+20u,0u);off+=PKG_EOCD_BYTES;
    if(off!=need){st=ODM_STATUS_INVARIANT_BROKEN;goto fail;}
    st=sha_data(manifest,manifest_bytes,&manifest_sha);if(st!=ODM_STATUS_OK)goto fail; st=sha_data(buffer,need,&package_sha);if(st!=ODM_STATUS_OK)goto fail;
    memset(&info,0,sizeof(info));info.schema_version_major=1u;info.schema_version_minor=0u;info.entry_count=listed_n;info.signature_algorithm=ODM_ODPARMS_SIGNATURE_ED25519;info.package_bytes=need;info.total_uncompressed_bytes=total;info.package_sha256=package_sha;info.manifest_sha256=manifest_sha;info.score_sha256=score_sha;info.capability_sha256=cap_sha;info.music_map_sha256=map_sha;info.music_policy_sha256=policy_sha;info.render_ir_sha256=ir_sha;memcpy(info.signer_public_key,pub,32u);*out_info=info; st=ODM_STATUS_OK;
fail:
    free(manifest);
    if(all){ for(i=0u;i<all_n;i++){ /* all[2+] aliases listed compressed pointers */ if(i<2u)free(all[i].compressed); } free(all); }
    free_entries(listed,listed_n);
    return st;
}

static odm_status manifest_parse(const uint8_t *m,uint64_t n,pkg_verified *v) {
    uint32_t count,i; uint64_t expected,total=0u;
    if(n<ODM_ODPARMS_MANIFEST_HEADER_BYTES||memcmp(m,PKG_MANIFEST_MAGIC,8u)!=0)return ODM_STATUS_INVALID_DATA;
    if(rd32(m+8u)!=1u||rd32(m+12u)!=0u)return ODM_STATUS_VERSION_MISMATCH;
    if(rd32(m+16u)!=0u||rd32(m+228u)!=ODM_ODPARMS_MANIFEST_ENTRY_BYTES||rd32(m+224u)!=ODM_ODPARMS_SIGNATURE_ED25519)return ODM_STATUS_INVALID_DATA;
    for(i=232u;i<256u;i++)if(m[i]!=0u)return ODM_STATUS_INVALID_DATA;
    count=rd32(m+20u);if(count<2u||count>ODM_ODPARMS_MAX_ENTRIES)return ODM_STATUS_INVALID_DATA;
    expected=ODM_ODPARMS_MANIFEST_HEADER_BYTES+(uint64_t)count*ODM_ODPARMS_MANIFEST_ENTRY_BYTES;if(expected!=n)return ODM_STATUS_INVALID_DATA;
    v->entries=(pkg_entry*)calloc(count,sizeof(*v->entries));if(!v->entries)return ODM_STATUS_OUT_OF_MEMORY;v->manifest_entry_count=count;
    memcpy(v->score_sha.bytes,m+32u,32u);memcpy(v->capability_sha.bytes,m+64u,32u);memcpy(v->music_map_sha.bytes,m+96u,32u);memcpy(v->music_policy_sha.bytes,m+128u,32u);memcpy(v->render_ir_sha.bytes,m+160u,32u);memcpy(v->public_key,m+192u,32u);
    if(digest_zero(&v->score_sha)||digest_zero(&v->capability_sha)||digest_zero(&v->music_map_sha)||digest_zero(&v->music_policy_sha)||digest_zero(&v->render_ir_sha))return ODM_STATUS_INVALID_DATA;
    for(i=0u;i<count;i++){
        const uint8_t *p=m+256u+(uint64_t)i*160u;pkg_entry *e=&v->entries[i];uint32_t j;
        e->kind=rd32(p);e->method=rd32(p+4u);e->flags=rd32(p+8u);e->resource_id=rd32(p+12u);e->data_bytes=rd64(p+16u);memcpy(e->sha256.bytes,p+24u,32u);e->path_len=rd32(p+56u);
        if(rd32(p+60u)!=0u||e->flags!=0u||e->path_len==0u||e->path_len>95u||e->data_bytes>ODM_ODPARMS_MAX_ENTRY_BYTES)return ODM_STATUS_INVALID_DATA;
        for(j=e->path_len;j<96u;j++)if(p[64u+j]!=0u)return ODM_STATUS_INVALID_DATA;
        {char *s=(char*)malloc((size_t)e->path_len+1u);if(!s)return ODM_STATUS_OUT_OF_MEMORY;memcpy(s,p+64u,e->path_len);s[e->path_len]='\0';e->path=s;if(!path_valid(s,NULL))return ODM_STATUS_INVALID_DATA;if(i>=2u&&reserved_path(s))return ODM_STATUS_INVALID_DATA;}
        if(i==0u){if(e->kind!=ODM_ODPARMS_ENTRY_RENDER_IR||e->method!=0u||strcmp(e->path,PKG_RENDER_NAME)!=0)return ODM_STATUS_INVALID_DATA;}
        else if(i==1u){if(e->kind!=ODM_ODPARMS_ENTRY_ANALYSIS||e->method!=0u||strcmp(e->path,PKG_ANALYSIS_NAME)!=0)return ODM_STATUS_INVALID_DATA;}
        else {if(e->kind==ODM_ODPARMS_ENTRY_MEDIA){if(e->method!=0u||e->resource_id==0u)return ODM_STATUS_INVALID_DATA;}else if(e->kind==ODM_ODPARMS_ENTRY_DATA){if(e->method!=8u||e->resource_id!=0u)return ODM_STATUS_INVALID_DATA;}else return ODM_STATUS_INVALID_DATA;if(strcmp(v->entries[i-1u].path,e->path)>=0)return ODM_STATUS_INVALID_DATA;}
        if(!add_u64(total,e->data_bytes,&total)||total>ODM_ODPARMS_MAX_TOTAL_UNCOMPRESSED)return ODM_STATUS_BUDGET_EXCEEDED;
    }
    if (total != rd64(m + 24u)) return ODM_STATUS_INVALID_DATA;
    v->total_uncompressed = total;
    return ODM_STATUS_OK;
}
static void verified_free(pkg_verified *v){uint32_t i;if(v&&v->entries){for(i=0u;i<v->manifest_entry_count;i++)free((void*)v->entries[i].path);free(v->entries);v->entries=NULL;}}

static odm_status verify_entry_payload(const pkg_entry *e,const uint8_t *payload) {
    odm_sha256_digest d;uint32_t crc;odm_status st;uint64_t req;
    if(e->method==0u){st=odm_pkg_crc32(payload,e->data_bytes,&crc);if(st!=ODM_STATUS_OK)return st;st=sha_data(payload,e->data_bytes,&d);if(st!=ODM_STATUS_OK)return st;}
    else {
        if(e->compressed_bytes==0u&&e->data_bytes!=0u)return ODM_STATUS_INVALID_DATA;
        if(e->compressed_bytes!=0u && e->data_bytes>ODM_DEFLATE_WINDOW &&
           e->data_bytes>e->compressed_bytes*(uint64_t)ODM_ODPARMS_MAX_EXPANSION_RATIO)return ODM_STATUS_BUDGET_EXCEEDED;
        st=odm_pkg_inflate_fixed(payload,e->compressed_bytes,NULL,0u,e->data_bytes,&req,&crc,&d);if(st!=ODM_STATUS_OK)return st;if(req!=e->data_bytes)return ODM_STATUS_INVALID_DATA;
    }
    if (crc != e->crc32 || !odm_sha256_equal(&d, &e->sha256)) return ODM_STATUS_INTEGRITY_ERROR;
    return ODM_STATUS_OK;
}

static odm_status verify_media_against_ir(const uint8_t *render_ir,uint64_t render_ir_bytes,
                                          const pkg_verified *v) {
    odm_ir_parsed parsed;
    const odm_ir_section_view *resources;
    uint32_t i,j,media_count=0u;
    odm_status st=odm_ir_parse_internal(render_ir,render_ir_bytes,&parsed);
    if(st!=ODM_STATUS_OK)return st;
    resources=&parsed.sections[ODM_IR_SECTION_RESOURCES-1u];
    for(i=2u;i<v->manifest_entry_count;i++)if(v->entries[i].kind==ODM_ODPARMS_ENTRY_MEDIA)media_count++;
    if(media_count!=resources->count)return ODM_STATUS_INTEGRITY_ERROR;
    for(i=0u;i<resources->count;i++) {
        const uint8_t *r=resources->records+(uint64_t)i*resources->record_bytes;
        uint32_t id=odm_ir_read_u32(r);
        uint32_t found=0u;
        odm_sha256_digest resource_sha;
        memcpy(resource_sha.bytes,r+16u,32u);
        for(j=2u;j<v->manifest_entry_count;j++) {
            const pkg_entry *e=&v->entries[j];
            if(e->kind==ODM_ODPARMS_ENTRY_MEDIA&&e->resource_id==id) {
                found++;
                if(!odm_sha256_equal(&e->sha256,&resource_sha))return ODM_STATUS_INTEGRITY_ERROR;
            }
        }
        if(found!=1u)return ODM_STATUS_INTEGRITY_ERROR;
    }
    for(i=2u;i<v->manifest_entry_count;i++)if(v->entries[i].kind==ODM_ODPARMS_ENTRY_MEDIA) {
        uint32_t found=0u;
        for(j=0u;j<resources->count;j++) {
            const uint8_t *r=resources->records+(uint64_t)j*resources->record_bytes;
            if(odm_ir_read_u32(r)==v->entries[i].resource_id)found++;
        }
        if(found!=1u)return ODM_STATUS_INTEGRITY_ERROR;
    }
    return ODM_STATUS_OK;
}

static odm_status verify_internal(const uint8_t *p,uint64_t n,const uint8_t expected_pk[32],pkg_verified *v) {
    uint64_t eocd_off,central_off,central_bytes,cursor,local_expected=0u;uint32_t all_n,i;odm_status st;pkg_entry *centr=NULL;odm_sha256_digest current_policy,manifest_sha;
    if (!p || !v) return ODM_STATUS_INVALID_ARGUMENT;
    memset(v, 0, sizeof(*v));
    v->package = p;
    v->package_size = n;
    if (n < PKG_EOCD_BYTES || n > ODM_ODPARMS_MAX_PACKAGE_BYTES) return ODM_STATUS_INVALID_DATA;
    eocd_off = n - PKG_EOCD_BYTES;
    if(rd32(p+eocd_off)!=UINT32_C(0x06054b50)||rd16(p+eocd_off+4u)!=0u||rd16(p+eocd_off+6u)!=0u||rd16(p+eocd_off+8u)!=rd16(p+eocd_off+10u)||rd16(p+eocd_off+20u)!=0u)return ODM_STATUS_INVALID_DATA;
    all_n=rd16(p+eocd_off+10u);if(all_n<4u||all_n>PKG_MAX_ALL_ENTRIES)return ODM_STATUS_INVALID_DATA;central_bytes=rd32(p+eocd_off+12u);central_off=rd32(p+eocd_off+16u);if(central_off+central_bytes!=eocd_off)return ODM_STATUS_INVALID_DATA;
    centr=(pkg_entry*)calloc(all_n,sizeof(*centr));if(!centr)return ODM_STATUS_OUT_OF_MEMORY;cursor=central_off;
    for(i=0u;i<all_n;i++){
        pkg_entry *e=&centr[i];uint16_t name_len,extra,comment;char *name;
        if(cursor+46u>eocd_off||rd32(p+cursor)!=UINT32_C(0x02014b50)){st=ODM_STATUS_INVALID_DATA;goto fail;}
        if(rd16(p+cursor+4u)!=PKG_ZIP_VERSION||rd16(p+cursor+6u)!=PKG_ZIP_VERSION||rd16(p+cursor+8u)!=0u||rd16(p+cursor+12u)!=0u||rd16(p+cursor+14u)!=0u||rd16(p+cursor+34u)!=0u||rd16(p+cursor+36u)!=0u||rd32(p+cursor+38u)!=0u){st=ODM_STATUS_INVALID_DATA;goto fail;}
        e->method=rd16(p+cursor+10u);if(e->method!=0u&&e->method!=8u){st=ODM_STATUS_UNSUPPORTED;goto fail;}e->crc32=rd32(p+cursor+16u);e->compressed_bytes=rd32(p+cursor+20u);e->data_bytes=rd32(p+cursor+24u);if(e->method==0u&&e->compressed_bytes!=e->data_bytes){st=ODM_STATUS_INVALID_DATA;goto fail;}name_len=rd16(p+cursor+28u);extra=rd16(p+cursor+30u);comment=rd16(p+cursor+32u);e->local_offset=rd32(p+cursor+42u);
        if(extra!=0u||comment!=0u||name_len==0u||name_len>95u||cursor+46u+name_len>eocd_off){st=ODM_STATUS_INVALID_DATA;goto fail;}name=(char*)malloc((size_t)name_len+1u);if(!name){st=ODM_STATUS_OUT_OF_MEMORY;goto fail;}memcpy(name,p+cursor+46u,name_len);name[name_len]='\0';e->path=name;e->path_len=name_len;if(!path_valid(name,NULL)){st=ODM_STATUS_INVALID_DATA;goto fail;}cursor+=46u+name_len;
        if(e->local_offset!=local_expected||local_expected+30u+name_len>central_off||rd32(p+local_expected)!=UINT32_C(0x04034b50)||rd16(p+local_expected+4u)!=PKG_ZIP_VERSION||rd16(p+local_expected+6u)!=0u||rd16(p+local_expected+8u)!=(uint16_t)e->method||rd16(p+local_expected+10u)!=0u||rd16(p+local_expected+12u)!=0u||rd32(p+local_expected+14u)!=e->crc32||rd32(p+local_expected+18u)!=(uint32_t)e->compressed_bytes||rd32(p+local_expected+22u)!=(uint32_t)e->data_bytes||rd16(p+local_expected+26u)!=name_len||rd16(p+local_expected+28u)!=0u||memcmp(p+local_expected+30u,name,name_len)!=0){st=ODM_STATUS_INVALID_DATA;goto fail;}
        if(!add_u64(local_expected,30u+name_len+e->compressed_bytes,&local_expected)||local_expected>central_off){st=ODM_STATUS_INVALID_DATA;goto fail;}
    }
    if(cursor!=eocd_off||local_expected!=central_off){st=ODM_STATUS_INVALID_DATA;goto fail;}
    if(strcmp(centr[0].path,PKG_MANIFEST_NAME)!=0||centr[0].method!=0u||strcmp(centr[1].path,PKG_SIGNATURE_NAME)!=0||centr[1].method!=0u||centr[1].data_bytes!=64u||strcmp(centr[2].path,PKG_RENDER_NAME)!=0||centr[2].method!=0u||strcmp(centr[3].path,PKG_ANALYSIS_NAME)!=0||centr[3].method!=0u){st=ODM_STATUS_INVALID_DATA;goto fail;}
    v->manifest=p+centr[0].local_offset+30u+centr[0].path_len;v->manifest_bytes=centr[0].data_bytes;v->signature=p+centr[1].local_offset+30u+centr[1].path_len;
    {uint32_t crc;st=odm_pkg_crc32(v->manifest,v->manifest_bytes,&crc);if(st!=ODM_STATUS_OK||crc!=centr[0].crc32){if(st==ODM_STATUS_OK)st=ODM_STATUS_INTEGRITY_ERROR;goto fail;}st=sha_data(v->manifest,v->manifest_bytes,&manifest_sha);if(st!=ODM_STATUS_OK)goto fail;}
    {uint32_t crc;st=odm_pkg_crc32(v->signature,64u,&crc);if(st!=ODM_STATUS_OK||crc!=centr[1].crc32){if(st==ODM_STATUS_OK)st=ODM_STATUS_INTEGRITY_ERROR;goto fail;}}
    st=manifest_parse(v->manifest,v->manifest_bytes,v);if(st!=ODM_STATUS_OK)goto fail;if(v->manifest_entry_count+2u!=all_n){st=ODM_STATUS_INVALID_DATA;goto fail;}
    if(expected_pk&&!bytes_equal_ct(expected_pk,v->public_key,32u)){st=ODM_STATUS_INTEGRITY_ERROR;goto fail;}st=odm_pkg_ed25519_verify(v->public_key,v->manifest,v->manifest_bytes,v->signature);if(st!=ODM_STATUS_OK)goto fail;
    for(i=0u;i<v->manifest_entry_count;i++){
        pkg_entry *me=&v->entries[i],*ce=&centr[i+2u];const uint8_t *payload;
        if(strcmp(me->path,ce->path)!=0||me->method!=ce->method||me->data_bytes!=ce->data_bytes){st=ODM_STATUS_INTEGRITY_ERROR;goto fail;}me->compressed_bytes=ce->compressed_bytes;me->crc32=ce->crc32;me->local_offset=ce->local_offset;payload=p+ce->local_offset+30u+ce->path_len;st=verify_entry_payload(me,payload);if(st!=ODM_STATUS_OK)goto fail;
    }
    st=odm_music_policy_current_sha256(&current_policy);if(st!=ODM_STATUS_OK)goto fail;if(!odm_sha256_equal(&current_policy,&v->music_policy_sha)){st=ODM_STATUS_VERSION_MISMATCH;goto fail;}
    {odm_render_ir_info ri;const pkg_entry *e=&v->entries[0];const uint8_t *payload=p+e->local_offset+30u+e->path_len;st=odm_render_ir_validate(payload,e->data_bytes,&v->score_sha,&v->music_policy_sha,&ri);if(st!=ODM_STATUS_OK)goto fail;if(!odm_sha256_equal(&ri.record_sha256,&v->render_ir_sha)||!odm_sha256_equal(&ri.capability_sha256,&v->capability_sha)||!odm_sha256_equal(&ri.music_map_sha256,&v->music_map_sha)){st=ODM_STATUS_INTEGRITY_ERROR;goto fail;}
        st=verify_media_against_ir(payload,e->data_bytes,v);if(st!=ODM_STATUS_OK)goto fail;
        {odm_music_map_info mi;uint64_t ticks;const pkg_entry *a=&v->entries[1];const uint8_t *ap=p+a->local_offset+30u+a->path_len;st=odm_music_map_record_validate(ap,a->data_bytes,&v->music_policy_sha,&mi,&ticks);if(st!=ODM_STATUS_OK)goto fail;(void)ticks;if(!odm_sha256_equal(&mi.record_sha256,&v->music_map_sha)||mi.canonical_frames!=ri.canonical_music_frames){st=ODM_STATUS_INTEGRITY_ERROR;goto fail;}}
    }
    st=ODM_STATUS_OK;
fail:
    if(centr){for(i=0u;i<all_n;i++)free((void*)centr[i].path);free(centr);}if(st!=ODM_STATUS_OK)verified_free(v);return st;
}

static odm_status verified_info_build(const uint8_t *package_bytes, uint64_t package_size,
                                      const pkg_verified *v, odm_odparms_info *out_info) {
    odm_sha256_digest pkg, man;
    odm_odparms_info info;
    odm_status st;
    if (!package_bytes || !v || !out_info) return ODM_STATUS_INVALID_ARGUMENT;
    st = sha_data(package_bytes, package_size, &pkg);
    if (st != ODM_STATUS_OK) return st;
    st = sha_data(v->manifest, v->manifest_bytes, &man);
    if (st != ODM_STATUS_OK) return st;
    memset(&info, 0, sizeof(info));
    info.schema_version_major = ODM_ODPARMS_SCHEMA_MAJOR;
    info.schema_version_minor = ODM_ODPARMS_SCHEMA_MINOR;
    info.entry_count = v->manifest_entry_count;
    info.signature_algorithm = ODM_ODPARMS_SIGNATURE_ED25519;
    info.package_bytes = package_size;
    info.total_uncompressed_bytes = v->total_uncompressed;
    info.package_sha256 = pkg;
    info.manifest_sha256 = man;
    info.score_sha256 = v->score_sha;
    info.capability_sha256 = v->capability_sha;
    info.music_map_sha256 = v->music_map_sha;
    info.music_policy_sha256 = v->music_policy_sha;
    info.render_ir_sha256 = v->render_ir_sha;
    memcpy(info.signer_public_key, v->public_key, ODM_ODPARMS_PUBLIC_KEY_BYTES);
    *out_info = info;
    return ODM_STATUS_OK;
}

odm_status odm_odparms_verify(const uint8_t *package_bytes, uint64_t package_size,
                               const uint8_t expected_public_key[32],
                               odm_odparms_info *out_info) {
    pkg_verified v;
    odm_status st;
    if (!out_info) return ODM_STATUS_INVALID_ARGUMENT;
    st = verify_internal(package_bytes, package_size, expected_public_key, &v);
    if (st != ODM_STATUS_OK) return st;
    st = verified_info_build(package_bytes, package_size, &v, out_info);
    verified_free(&v);
    return st;
}

static odm_status content_hash_u32(odm_sha256_context *ctx, uint32_t value) {
    uint8_t b[4];
    wr32(b, value);
    return odm_sha256_update(ctx, b, sizeof(b));
}

static odm_status content_hash_u64(odm_sha256_context *ctx, uint64_t value) {
    uint8_t b[8];
    wr64(b, value);
    return odm_sha256_update(ctx, b, sizeof(b));
}

odm_status odm_odparms_content_sha256(
    const uint8_t *package_bytes, uint64_t package_size,
    const uint8_t expected_public_key[ODM_ODPARMS_PUBLIC_KEY_BYTES],
    odm_sha256_digest *out_content_sha256) {
    static const uint8_t domain[] = "ODPAR_ODPARMS_CONTENT_V1";
    pkg_verified v;
    odm_sha256_context ctx = ODM_SHA256_CONTEXT_INITIALIZER;
    odm_status st;
    uint32_t i;
    if (!out_content_sha256) return ODM_STATUS_INVALID_ARGUMENT;
    st = verify_internal(package_bytes, package_size, expected_public_key, &v);
    if (st != ODM_STATUS_OK) return st;
    st = odm_sha256_init(&ctx);
    if (st == ODM_STATUS_OK) st = odm_sha256_update(&ctx, domain, sizeof(domain));
    if (st == ODM_STATUS_OK) st = content_hash_u32(&ctx, ODM_ODPARMS_SCHEMA_MAJOR);
    if (st == ODM_STATUS_OK) st = content_hash_u32(&ctx, ODM_ODPARMS_SCHEMA_MINOR);
    if (st == ODM_STATUS_OK) st = content_hash_u32(&ctx, v.manifest_entry_count);
    if (st == ODM_STATUS_OK) st = content_hash_u64(&ctx, v.total_uncompressed);
    if (st == ODM_STATUS_OK) st = odm_sha256_update(&ctx, v.score_sha.bytes, ODM_SHA256_DIGEST_BYTES);
    if (st == ODM_STATUS_OK) st = odm_sha256_update(&ctx, v.capability_sha.bytes, ODM_SHA256_DIGEST_BYTES);
    if (st == ODM_STATUS_OK) st = odm_sha256_update(&ctx, v.music_map_sha.bytes, ODM_SHA256_DIGEST_BYTES);
    if (st == ODM_STATUS_OK) st = odm_sha256_update(&ctx, v.music_policy_sha.bytes, ODM_SHA256_DIGEST_BYTES);
    if (st == ODM_STATUS_OK) st = odm_sha256_update(&ctx, v.render_ir_sha.bytes, ODM_SHA256_DIGEST_BYTES);
    for (i = 0u; st == ODM_STATUS_OK && i < v.manifest_entry_count; ++i) {
        const pkg_entry *e = &v.entries[i];
        st = content_hash_u32(&ctx, e->kind);
        if (st == ODM_STATUS_OK) st = content_hash_u32(&ctx, e->method);
        if (st == ODM_STATUS_OK) st = content_hash_u32(&ctx, e->flags);
        if (st == ODM_STATUS_OK) st = content_hash_u32(&ctx, e->resource_id);
        if (st == ODM_STATUS_OK) st = content_hash_u64(&ctx, e->data_bytes);
        if (st == ODM_STATUS_OK) st = odm_sha256_update(&ctx, e->sha256.bytes, ODM_SHA256_DIGEST_BYTES);
        if (st == ODM_STATUS_OK) st = content_hash_u32(&ctx, e->path_len);
        if (st == ODM_STATUS_OK) st = odm_sha256_update(&ctx, e->path, e->path_len);
    }
    if (st == ODM_STATUS_OK) st = odm_sha256_final(&ctx, out_content_sha256);
    verified_free(&v);
    return st;
}

odm_status odm_odparms_verified_core_views(
    const uint8_t *package_bytes, uint64_t package_size,
    const uint8_t expected_public_key[32], odm_odparms_info *out_info,
    odm_odparms_core_views *out_views) {
    pkg_verified v;
    odm_odparms_info info;
    odm_odparms_core_views views;
    const pkg_entry *render_entry;
    const pkg_entry *analysis_entry;
    odm_status st;
    if (!out_info || !out_views) return ODM_STATUS_INVALID_ARGUMENT;
    st = verify_internal(package_bytes, package_size, expected_public_key, &v);
    if (st != ODM_STATUS_OK) return st;
    st = verified_info_build(package_bytes, package_size, &v, &info);
    if (st != ODM_STATUS_OK) { verified_free(&v); return st; }
    if (v.manifest_entry_count < 2u) { verified_free(&v); return ODM_STATUS_INVARIANT_BROKEN; }
    render_entry = &v.entries[0];
    analysis_entry = &v.entries[1];
    if (render_entry->method != ODM_ODPARMS_METHOD_STORE ||
        analysis_entry->method != ODM_ODPARMS_METHOD_STORE) {
        verified_free(&v);
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    memset(&views, 0, sizeof(views));
    views.render_ir = package_bytes + render_entry->local_offset + PKG_LOCAL_BYTES + render_entry->path_len;
    views.render_ir_bytes = render_entry->data_bytes;
    views.analysis_bin = package_bytes + analysis_entry->local_offset + PKG_LOCAL_BYTES + analysis_entry->path_len;
    views.analysis_bin_bytes = analysis_entry->data_bytes;
    *out_info = info;
    *out_views = views;
    verified_free(&v);
    return ODM_STATUS_OK;
}

odm_status odm_odparms_extract(const uint8_t *package_bytes,uint64_t package_size,const uint8_t expected_public_key[32],const char *path,uint8_t *buffer,uint64_t capacity,uint64_t *out_required){pkg_verified v;odm_status st;uint32_t i;if(!path||!out_required)return ODM_STATUS_INVALID_ARGUMENT;st=verify_internal(package_bytes,package_size,expected_public_key,&v);if(st!=ODM_STATUS_OK)return st;for(i=0u;i<v.manifest_entry_count;i++)if(strcmp(path,v.entries[i].path)==0){pkg_entry *e=&v.entries[i];const uint8_t *payload=package_bytes+e->local_offset+30u+e->path_len;*out_required=e->data_bytes;if(!buffer||capacity<e->data_bytes){verified_free(&v);return ODM_STATUS_BUFFER_TOO_SMALL;}if(e->method==0u){memcpy(buffer,payload,(size_t)e->data_bytes);st=ODM_STATUS_OK;}else{uint64_t req;uint32_t crc;odm_sha256_digest d;st=odm_pkg_inflate_fixed(payload,e->compressed_bytes,buffer,capacity,e->data_bytes,&req,&crc,&d);}verified_free(&v);return st;}verified_free(&v);return ODM_STATUS_INVALID_DATA;}
