#!/usr/bin/env python3
"""Independent Gate-5 .odparms oracle.

A small C probe builds a canonical signed package only through public engine APIs.
Python then parses the ZIP32 structure itself, independently verifies CRC/SHA-256,
raw DEFLATE, the binary manifest, Ed25519, Music Policy binding and Render-IR
resource binding. Expected ZIP/manifest bytes never come from the engine parser.
"""
from __future__ import annotations
import argparse, binascii, hashlib, pathlib, struct, subprocess, tempfile, zlib

# Identidad de la politica de musica v2. NO se toma del arbol bajo prueba:
# es la que reconstruye byte a byte tools/check_music_analysis_oracle.py a
# partir de la especificacion. La v1 valia cbe9ccd3...; cambio al publicar
# las razones con signo en el rango simetrico [-INT32_MAX, +INT32_MAX].
POLICY_SHA = bytes.fromhex("0a038fb13f700d4002a3b534dff1e4414c081a67caf63454079f9367ce23fdac")
RFC_PK = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
EXPECTED_NAMES = ["manifest.odm", "signature.ed25519", "render.ir", "analysis.bin", "data/readme.txt", "media/image.bin"]
EXPECTED_METHODS = [0, 0, 0, 0, 8, 0]
DATA = b"ODPAR Music Gate 5 oracle data " * 64
MEDIA = b"gate5-oracle-image-resource"

PROBE = r'''#include "odm_package.h"
#include "odm_music_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const unsigned char seed[32]={0x9d,0x61,0xb1,0x9d,0xef,0xfd,0x5a,0x60,0xba,0x84,0x4a,0xf4,0x92,0xec,0x2c,0xc4,0x44,0x49,0xc5,0x69,0x7b,0x32,0x69,0x19,0x70,0x3b,0xac,0x03,0x1c,0xae,0x7f,0x60};
static const unsigned char media[]="gate5-oracle-image-resource";
static const unsigned char data[]="ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ODPAR Music Gate 5 oracle data ";
static void node(odm_score_node*n,unsigned id,unsigned scene,unsigned role,unsigned cap,unsigned res,int z){memset(n,0,sizeof(*n));n->node_id=id;n->scene_id=scene;n->role=role;n->capability_id=cap;n->capability_major=1;n->resource_id=res;n->state_model=ODM_STATELESS;n->z_index=z;n->end_sample=4800;n->scale_x_micro=ODM_MICRO_ONE;n->scale_y_micro=ODM_MICRO_ONE;n->opacity_micro=ODM_MICRO_ONE;n->seed_domain=id;}
int main(int argc,char**argv){int32_t*w=0;odm_music_complex_q31*t=0;odm_music_analysis_tick*ticks=0;odm_music_analysis_plan plan={0};odm_sha256_digest pcm,map;uint64_t tc=0,ab=0,ib=0,pb=0;uint8_t*analysis=0,*ir=0,*pkg=0;odm_score_header h={0};odm_score_resource r={0};odm_score_act a={0};odm_score_scene sc={0};odm_score_node n[2];odm_score_view score={0};odm_music_map_binding bind={0};odm_odparms_asset assets[2]={{0}};odm_odparms_info info;FILE*f;uint64_t i;if(argc!=2)return 2;
w=malloc((size_t)ODM_MUSIC_WINDOW_SAMPLES*sizeof(*w));t=malloc((size_t)ODM_MUSIC_FFT_TWIDDLES*sizeof(*t));if(!w||!t)return 3;if(odm_music_analysis_plan_build(w,ODM_MUSIC_WINDOW_SAMPLES,t,ODM_MUSIC_FFT_TWIDDLES,&plan))return 4;if(odm_music_tick_count(4800,&tc))return 5;ticks=calloc((size_t)tc,sizeof(*ticks));if(!ticks)return 6;for(i=0;i<tc;i++){ticks[i].tick_index=i;ticks[i].center_sample=i*ODM_MUSIC_TICK_SAMPLES;ticks[i].silence=1;}if(odm_sha256("gate5-oracle-pcm",16,&pcm))return 7;if(odm_music_map_record_write(&plan,4800,&pcm,ticks,tc,0,0,&ab,0)!=ODM_STATUS_BUFFER_TOO_SMALL)return 8;analysis=malloc((size_t)ab);if(!analysis)return 9;if(odm_music_map_record_write(&plan,4800,&pcm,ticks,tc,analysis,ab,&ab,&map))return 10;
h.schema_version_major=ODM_SCORE_SCHEMA_MAJOR;h.schema_version_minor=ODM_SCORE_SCHEMA_MINOR;h.fps=30;h.width=1920;h.height=1080;h.project_end_sample=4800;h.project_seed=0x55;h.resource_count=1;h.act_count=1;h.scene_count=1;h.node_count=2;r.resource_id=1;r.kind=ODM_SCORE_RESOURCE_IMAGE;if(odm_sha256(media,sizeof(media)-1,&r.content_sha256))return 11;a.act_id=1;a.end_sample=4800;sc.scene_id=1;sc.act_id=1;sc.environment_node_id=1;sc.primary_node_id=2;sc.end_sample=4800;node(&n[0],1,1,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,0,-100);node(&n[1],2,1,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_IMAGE,1,0);score.header=&h;score.resources=&r;score.acts=&a;score.scenes=&sc;score.nodes=n;bind.canonical_frames=4800;bind.music_map_sha256=map;bind.music_policy_sha256=plan.policy_sha256;if(odm_score_compile_render_ir(&score,&bind,0,0,&ib,0)!=ODM_STATUS_BUFFER_TOO_SMALL)return 12;ir=malloc((size_t)ib);if(!ir)return 13;if(odm_score_compile_render_ir(&score,&bind,ir,ib,&ib,0))return 14;assets[0].kind=ODM_ODPARMS_ENTRY_MEDIA;assets[0].resource_id=1;assets[0].path="media/image.bin";assets[0].data=media;assets[0].data_bytes=sizeof(media)-1;assets[1].kind=ODM_ODPARMS_ENTRY_DATA;assets[1].path="data/readme.txt";assets[1].data=data;assets[1].data_bytes=sizeof(data)-1;if(odm_odparms_build(&score,ir,ib,analysis,ab,assets,2,seed,0,0,&pb,&info)!=ODM_STATUS_BUFFER_TOO_SMALL)return 15;pkg=malloc((size_t)pb);if(!pkg)return 16;if(odm_odparms_build(&score,ir,ib,analysis,ab,assets,2,seed,pkg,pb,&pb,&info))return 17;f=fopen(argv[1],"wb");if(!f)return 18;if(fwrite(pkg,1,(size_t)pb,f)!=(size_t)pb)return 19;if(fclose(f))return 20;free(pkg);free(ir);free(analysis);free(ticks);free(t);free(w);return 0;}
'''

def u16(b,o): return struct.unpack_from('<H',b,o)[0]
def u32(b,o): return struct.unpack_from('<I',b,o)[0]
def u64(b,o): return struct.unpack_from('<Q',b,o)[0]

def parse_pkg(raw: bytes):
    if len(raw) < 22: raise AssertionError('short package')
    eo=len(raw)-22
    if raw[eo:eo+4]!=b'PK\x05\x06': raise AssertionError('EOCD signature')
    if u16(raw,eo+4) or u16(raw,eo+6) or u16(raw,eo+20): raise AssertionError('non-canonical EOCD')
    count=u16(raw,eo+10)
    if u16(raw,eo+8)!=count: raise AssertionError('split-disk count')
    coff=u32(raw,eo+16); cbytes=u32(raw,eo+12)
    if coff+cbytes!=eo: raise AssertionError('central extent')
    entries=[]; cur=coff; local_expected=0
    for idx in range(count):
        if raw[cur:cur+4]!=b'PK\x01\x02': raise AssertionError(f'central sig {idx}')
        made=u16(raw,cur+4); need=u16(raw,cur+6); flags=u16(raw,cur+8); method=u16(raw,cur+10)
        mtime=u16(raw,cur+12); mdate=u16(raw,cur+14); crc=u32(raw,cur+16); csz=u32(raw,cur+20); usz=u32(raw,cur+24)
        nl=u16(raw,cur+28); ex=u16(raw,cur+30); cm=u16(raw,cur+32); disk=u16(raw,cur+34); iattr=u16(raw,cur+36); eattr=u32(raw,cur+38); lo=u32(raw,cur+42)
        if (made,need,flags,mtime,mdate,ex,cm,disk,iattr,eattr)!=(20,20,0,0,0,0,0,0,0,0): raise AssertionError(f'non-canonical central {idx}')
        name=raw[cur+46:cur+46+nl].decode('ascii')
        if lo!=local_expected: raise AssertionError(f'non-contiguous local {idx}')
        if raw[lo:lo+4]!=b'PK\x03\x04': raise AssertionError('local sig')
        if (u16(raw,lo+4),u16(raw,lo+6),u16(raw,lo+8),u16(raw,lo+10),u16(raw,lo+12),u32(raw,lo+14),u32(raw,lo+18),u32(raw,lo+22),u16(raw,lo+26),u16(raw,lo+28)) != (20,0,method,0,0,crc,csz,usz,nl,0): raise AssertionError(f'local mismatch {idx}')
        if raw[lo+30:lo+30+nl].decode('ascii')!=name: raise AssertionError('name mismatch')
        po=lo+30+nl; comp=raw[po:po+csz]
        if method==0:
            data=comp
            if csz!=usz: raise AssertionError('STORE size mismatch')
        elif method==8:
            data=zlib.decompress(comp,-15)
        else: raise AssertionError('method')
        if len(data)!=usz or (binascii.crc32(data)&0xffffffff)!=crc: raise AssertionError(f'payload CRC/size {name}')
        entries.append(dict(name=name,method=method,crc=crc,comp=comp,data=data,usz=usz,lo=lo,central=cur))
        local_expected=po+csz; cur+=46+nl
    if cur!=eo or local_expected!=coff: raise AssertionError('extent gap/overlap')
    return entries

def parse_manifest(m: bytes):
    if len(m)<256 or m[:8]!=b'ODPARMS\0': raise AssertionError('manifest magic')
    if (u32(m,8),u32(m,12),u32(m,16),u32(m,224),u32(m,228))!=(1,0,0,1,160): raise AssertionError('manifest fixed header')
    if any(m[232:256]): raise AssertionError('manifest reserved')
    count=u32(m,20)
    if len(m)!=256+count*160: raise AssertionError('manifest extent')
    out=[]
    for i in range(count):
        p=256+i*160; plen=u32(m,p+56)
        if u32(m,p+60) or any(m[p+64+plen:p+160]): raise AssertionError('manifest entry reserved')
        out.append(dict(kind=u32(m,p),method=u32(m,p+4),flags=u32(m,p+8),rid=u32(m,p+12),size=u64(m,p+16),sha=m[p+24:p+56],path=m[p+64:p+64+plen].decode('ascii')))
    return out

def render_resources(ir: bytes):
    if ir[:4]!=b'ODMC' or ir[64:72]!=b'ODRIRV0\0': raise AssertionError('IR envelope')
    if hashlib.sha256(ir[64:]).digest()!=ir[32:64]: raise AssertionError('IR ODMC digest')
    pos=64+256
    resources=[]
    expected_sizes=[32,64,64,128,64,64,64,64,64]
    for sid,rb in enumerate(expected_sizes,1):
        got_sid,ver,count,got_rb=struct.unpack_from('<IIII',ir,pos); pos+=16
        if (got_sid,ver,got_rb)!=(sid,1,rb): raise AssertionError('IR section shape')
        if sid==2:
            for j in range(count):
                r=ir[pos+j*rb:pos+(j+1)*rb]
                resources.append((u32(r,0),u32(r,4),r[16:48]))
        pos+=count*rb
    if pos!=len(ir): raise AssertionError('IR trailing bytes')
    return resources

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--root',required=True); ap.add_argument('--cc',default='gcc'); ap.add_argument('--library',required=True); args=ap.parse_args()
    root=pathlib.Path(args.root).resolve(); lib=pathlib.Path(args.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm_g5_oracle_') as td_s:
        td=pathlib.Path(td_s); c=td/'probe.c'; exe=td/'probe'; p1=td/'a.odparms'; p2=td/'b.odparms'; c.write_text(PROBE)
        cmd=[args.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(c),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        subprocess.run(cmd,check=True,cwd=root)
        subprocess.run([str(exe),str(p1)],check=True,cwd=root); subprocess.run([str(exe),str(p2)],check=True,cwd=root)
        raw=p1.read_bytes(); raw2=p2.read_bytes()
    if raw!=raw2: raise SystemExit('odparms determinism mismatch across independent builds')
    entries=parse_pkg(raw)
    if [e['name'] for e in entries]!=EXPECTED_NAMES: raise SystemExit(f"entry order mismatch: {[e['name'] for e in entries]}")
    if [e['method'] for e in entries]!=EXPECTED_METHODS: raise SystemExit('method policy mismatch')
    manifest=entries[0]['data']; sig=entries[1]['data']; mentries=parse_manifest(manifest)
    if len(sig)!=64 or manifest[192:224]!=RFC_PK: raise SystemExit('signer public key mismatch')
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
        Ed25519PublicKey.from_public_bytes(RFC_PK).verify(sig,manifest)
    except Exception as exc: raise SystemExit(f'independent Ed25519 verify failed: {exc}')
    expected_manifest_paths=EXPECTED_NAMES[2:]
    if [e['path'] for e in mentries]!=expected_manifest_paths: raise SystemExit('manifest path order mismatch')
    for me,ze in zip(mentries,entries[2:]):
        if me['method']!=ze['method'] or me['size']!=len(ze['data']) or me['sha']!=hashlib.sha256(ze['data']).digest(): raise SystemExit(f"manifest content mismatch: {me['path']}")
    if mentries[2]['kind']!=4 or mentries[2]['rid']!=0 or mentries[3]['kind']!=3 or mentries[3]['rid']!=1: raise SystemExit('asset kind/resource contract mismatch')
    if entries[4]['data']!=DATA or entries[5]['data']!=MEDIA: raise SystemExit('asset content mismatch')
    if manifest[128:160]!=POLICY_SHA: raise SystemExit('manifest music policy drift')
    analysis=entries[3]['data']; ir=entries[2]['data']
    if analysis[160:192]!=POLICY_SHA: raise SystemExit('analysis policy drift')
    if ir[248:280]!=POLICY_SHA: raise SystemExit('IR policy drift')
    resources=render_resources(ir)
    if resources!=[(1,1,hashlib.sha256(MEDIA).digest())]: raise SystemExit('IR resource binding mismatch')
    if mentries[3]['sha']!=resources[0][2]: raise SystemExit('manifest MEDIA != IR resource SHA')
    total=sum(e['size'] for e in mentries)
    if u64(manifest,24)!=total: raise SystemExit('manifest total uncompressed mismatch')
    print(f'.odparms oracle: pass ({len(raw)} bytes, {len(entries)} ZIP entries, {len(mentries)} signed manifest entries)')
    print(f'Package SHA-256: {hashlib.sha256(raw).hexdigest()}')
    print(f'Manifest SHA-256: {hashlib.sha256(manifest).hexdigest()}')
    print(f'Ed25519 public key: {RFC_PK.hex()}')
    print(f'Music policy SHA-256: {POLICY_SHA.hex()}')
    print(f'DATA raw={len(DATA)} deflate={len(entries[4]["comp"])}')

if __name__=='__main__': main()
