#include "odm_master.h"
#include "package_internal.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t CONTROLLER_MAGIC[8] = {'O','D','M','C','T','R','L','1'};
static const uint8_t CONTROLLER_DOMAIN[] = {'O','D','P','A','R','_','M','A','S','T','E','R','_','R','E','C','E','I','P','T','_','S','I','G','_','V','1',0};

static void wr32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static int eq_ct(const uint8_t *a,const uint8_t*b,uint32_t n){uint32_t i;uint8_t d=0u;for(i=0u;i<n;++i)d=(uint8_t)(d|(uint8_t)(a[i]^b[i]));return d==0u;}

static odm_status signed_message(const uint8_t *receipt,uint64_t receipt_bytes,uint8_t **out,uint64_t *out_bytes){
    uint64_t n;uint8_t *p;if(!receipt||!out||!out_bytes)return ODM_STATUS_INVALID_ARGUMENT;
    if(receipt_bytes>UINT64_MAX-sizeof(CONTROLLER_DOMAIN))return ODM_STATUS_OVERFLOW;
    n=sizeof(CONTROLLER_DOMAIN)+receipt_bytes;if(n>(uint64_t)SIZE_MAX)return ODM_STATUS_OVERFLOW;
    p=(uint8_t*)malloc((size_t)n);if(!p)return ODM_STATUS_OUT_OF_MEMORY;
    memcpy(p,CONTROLLER_DOMAIN,sizeof(CONTROLLER_DOMAIN));memcpy(p+sizeof(CONTROLLER_DOMAIN),receipt,(size_t)receipt_bytes);*out=p;*out_bytes=n;return ODM_STATUS_OK;
}

odm_status odm_controller_signature_public_from_seed(const uint8_t seed[32],uint8_t out_public_key[32]){return odm_pkg_ed25519_public_from_seed(seed,out_public_key);}

odm_status odm_controller_signature_write(const uint8_t seed[32],const uint8_t *receipt_record,uint64_t receipt_bytes,uint8_t out_envelope[ODM_CONTROLLER_SIGNATURE_BYTES]){
    uint8_t pk[32],sig[64],*msg=NULL;uint64_t msg_bytes=0u;odm_sha256_digest receipt_sha;odm_render_receipt_info info;odm_status st;
    if(!seed||!receipt_record||!out_envelope)return ODM_STATUS_INVALID_ARGUMENT;
    st=odm_render_receipt_validate(receipt_record,receipt_bytes,NULL,&info);if(st!=ODM_STATUS_OK)return st;
    st=odm_sha256(receipt_record,receipt_bytes,&receipt_sha);if(st!=ODM_STATUS_OK)return st;
    st=odm_pkg_ed25519_public_from_seed(seed,pk);if(st!=ODM_STATUS_OK)return st;
    st=signed_message(receipt_record,receipt_bytes,&msg,&msg_bytes);if(st!=ODM_STATUS_OK)return st;
    st=odm_pkg_ed25519_sign(seed,msg,msg_bytes,sig);free(msg);if(st!=ODM_STATUS_OK)return st;
    memset(out_envelope,0,ODM_CONTROLLER_SIGNATURE_BYTES);memcpy(out_envelope,CONTROLLER_MAGIC,8u);wr32(out_envelope+8u,ODM_CONTROLLER_SIGNATURE_SCHEMA_MAJOR);wr32(out_envelope+12u,ODM_CONTROLLER_SIGNATURE_SCHEMA_MINOR);wr32(out_envelope+16u,ODM_CONTROLLER_SIGNATURE_ED25519);wr32(out_envelope+20u,0u);memcpy(out_envelope+24u,receipt_sha.bytes,32u);memcpy(out_envelope+56u,pk,32u);memcpy(out_envelope+88u,sig,64u);return ODM_STATUS_OK;
}

odm_status odm_controller_signature_verify(const uint8_t expected_public_key[32],const uint8_t *receipt_record,uint64_t receipt_bytes,const uint8_t envelope[ODM_CONTROLLER_SIGNATURE_BYTES]){
    uint8_t *msg=NULL;uint64_t msg_bytes=0u;odm_sha256_digest receipt_sha;odm_render_receipt_info info;odm_status st;uint32_t i;
    if(!expected_public_key||!receipt_record||!envelope)return ODM_STATUS_INVALID_ARGUMENT;
    if(memcmp(envelope,CONTROLLER_MAGIC,8u)!=0||rd32(envelope+8u)!=ODM_CONTROLLER_SIGNATURE_SCHEMA_MAJOR||rd32(envelope+12u)!=ODM_CONTROLLER_SIGNATURE_SCHEMA_MINOR||rd32(envelope+16u)!=ODM_CONTROLLER_SIGNATURE_ED25519||rd32(envelope+20u)!=0u)return ODM_STATUS_INVALID_DATA;
    for(i=152u;i<ODM_CONTROLLER_SIGNATURE_BYTES;++i)if(envelope[i]!=0u)return ODM_STATUS_INVALID_DATA;
    if(!eq_ct(expected_public_key,envelope+56u,32u))return ODM_STATUS_INTEGRITY_ERROR;
    st=odm_render_receipt_validate(receipt_record,receipt_bytes,NULL,&info);if(st!=ODM_STATUS_OK)return st;
    st=odm_sha256(receipt_record,receipt_bytes,&receipt_sha);if(st!=ODM_STATUS_OK)return st;if(!eq_ct(receipt_sha.bytes,envelope+24u,32u))return ODM_STATUS_INTEGRITY_ERROR;
    st=signed_message(receipt_record,receipt_bytes,&msg,&msg_bytes);if(st!=ODM_STATUS_OK)return st;st=odm_pkg_ed25519_verify(envelope+56u,msg,msg_bytes,envelope+88u);free(msg);return st;
}
