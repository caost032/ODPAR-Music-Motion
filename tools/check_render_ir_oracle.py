#!/usr/bin/env python3
"""Independent Gate-4 Score -> Render IR oracle.

Builds one canonical score through the public C API, then independently rebuilds
its Score hash, capability hash, canonical Render-IR payload and ODMC envelope in
Python using only the frozen wire/math contracts. Expected bytes never come from
engine serialization helpers.
"""
from __future__ import annotations
import argparse, hashlib, os, pathlib, struct, subprocess, tempfile

# Identidad de la politica de musica v2. NO se toma del arbol bajo prueba:
# es la que reconstruye byte a byte tools/check_music_analysis_oracle.py a
# partir de la especificacion. La v1 valia cbe9ccd3...; cambio al publicar
# las razones con signo en el rango simetrico [-INT32_MAX, +INT32_MAX].
POLICY_SHA = bytes.fromhex('0a038fb13f700d4002a3b534dff1e4414c081a67caf63454079f9367ce23fdac')
Q32 = 1 << 32
Q31_MAX = (1 << 31) - 1

CAPS = [
    (0x00010001,1,0,0,1<<1,0),
    (0x00010002,1,0,0,1<<2,0),
    (0x00010003,1,0,0,1<<2,1),
    (0x00020001,1,0,0,0,0),
    (0x00020002,1,0,0,0,0),
    (0x00020003,1,0,0,0,0),
    (0x00020004,1,0,0,0,0),
    (0x00020005,1,0,0,0,0),
    (0x00030003,1,0,1,1<<5,0),
]

def u32(v): return struct.pack('<I', v & 0xffffffff)
def i32(v): return struct.pack('<i', v)
def u64(v): return struct.pack('<Q', v & 0xffffffffffffffff)
def i64(v): return struct.pack('<q', v)
def z(n): return bytes(n)

def score_hash() -> bytes:
    b=bytearray(b'ODPAR_SCORE_V1\0')
    # header (reserved deliberately excluded by contract)
    for v in (1,1,0): b += u32(v)
    b += i32(30)
    for v in (1920,1080): b += u32(v)
    b += i64(48000)+u64(0x123456789abcdef0)
    for v in (1,1,2,5,1,1,1,1,1): b += u32(v)
    image=hashlib.sha256(b'image-one').digest()
    b += u32(1)+u32(1)+u32(0)+image
    b += u32(1)+u32(0)+i64(0)+i64(48000)
    for s in ((1,1,1,2,0,30000,0),(2,1,3,4,24000,48000,0)):
        b += b''.join(u32(x) for x in s[:4])+i64(s[4])+i64(s[5])+u32(s[6])
    node_specs=[
      (1,1,1,0x10001,0,0,0,-100,0,30000,1_000_000),
      (2,1,2,0x10002,0,0,0,0,0,30000,1_000_000),
      (3,2,1,0x10001,0,0,0,-100,24000,48000,1_000_000),
      (4,2,2,0x10003,1,0,0,0,24000,48000,1_000_000),
      (5,2,5,0x30003,0,1,1,10,24000,48000,500_000),
    ]
    for nid,sid,role,cap,res,state,slot,zidx,start,end,opacity in node_specs:
        for v in (nid,sid,role,cap,1,0,res,state): b+=u32(v)
        b+=i32(zidx)+u32(0)+i64(start)+i64(end)
        for v in (0,0,1_000_000,1_000_000,opacity,0): b+=i64(v)
        b+=u64((nid*0x9e3779b97f4a7c15)&0xffffffffffffffff)
        for v in (slot,0,0,0,0): b+=u32(v)
    b += u32(1)+u32(1)+u32(1)+u32(0)+i64(1000)
    b += u32(1)+u32(5)+u32(5)+u32(3)+u32(0)+u32(0)+i64(64_000_000)+i64(0)
    b += u32(1)+u32(5)+u32(1)+u32(2)+u32(0)+i64(24000)+i64(48000)+i64(0)+i64(1_000_000)
    b += u32(1)+u32(1)+u32(2)+u32(1)+i64(24000)+i64(30000)+u32(0)
    b += u32(1)+u32(1)+u32(2)+u32(1)+u32(2)+u32(0)+i64(24000)
    return hashlib.sha256(b).digest()

def cap_hash() -> bytes:
    b=bytearray(b'ODPAR_CAPS_V1\0')+u32(len(CAPS))
    for cap in CAPS:
        for v in cap: b+=u32(v)
    return hashlib.sha256(b).digest()

def sec_header(sid,count,rb): return u32(sid)+u32(1)+u32(count)+u32(rb)
def timeline_rec(kind,rid,parent,start,end,a,b):
    return u32(kind)+u32(rid)+u32(parent)+u32(0)+i64(start)+i64(end)+u32(a)+u32(b)+z(24)

def node_rec(nid,sid,role,cap,res,state,slot,zidx,start,end,opacity):
    qop = Q31_MAX if opacity == 1_000_000 else 1<<30
    seed=(nid*0x9e3779b97f4a7c15)&0xffffffffffffffff
    return (u32(nid)+u32(sid)+u32(role)+u32(cap)+u32(1)+u32(0)+u32(res)+u32(state)+
            i32(zidx)+u32(0)+i64(start)+i64(end)+i64(0)+i64(0)+i64(Q32)+i64(Q32)+
            i32(qop)+u32(0)+u64(seed)+u32(slot)+u32(0)+u32(0)+u32(0)+u32(0)+u32(0))

def expected_record() -> tuple[bytes,bytes,bytes]:
    sh=score_hash(); ch=cap_hash(); mmap=hashlib.sha256(b'music-map').digest()
    p=bytearray(b'ODRIRV0\0')
    p+=u32(1)+u32(1)+u32(0)+i32(30)+u32(1920)+u32(1080)+u32(48000)+u32(9)
    p+=i64(1600)+i64(48000)+i64(30)+i64(48000)+u64(30000)+u64(0x123456789abcdef0)
    p+=sh+ch+mmap+POLICY_SHA+z(40)
    assert len(p)==256
    p+=sec_header(1,len(CAPS),32)
    for c in CAPS:
        p+=b''.join(u32(v) for v in c)+z(8)
    image=hashlib.sha256(b'image-one').digest()
    p+=sec_header(2,1,64)+u32(1)+u32(1)+u32(0)+u32(0)+image+z(16)
    p+=sec_header(3,4,64)
    p+=timeline_rec(1,1,0,0,48000,0,0)
    p+=timeline_rec(2,1,1,0,30000,1,2)
    p+=timeline_rec(2,2,1,24000,48000,3,4)
    p+=timeline_rec(3,1,1,1000,1000,1,0)
    p+=sec_header(4,5,128)
    p+=node_rec(1,1,1,0x10001,0,0,0,-100,0,30000,1_000_000)
    p+=node_rec(2,1,2,0x10002,0,0,0,0,0,30000,1_000_000)
    p+=node_rec(3,2,1,0x10001,0,0,0,-100,24000,48000,1_000_000)
    p+=node_rec(4,2,2,0x10003,1,0,0,0,24000,48000,1_000_000)
    p+=node_rec(5,2,5,0x30003,0,1,1,10,24000,48000,500_000)
    p+=sec_header(5,1,64)
    p+=u32(1)+u32(5)+u32(5)+u32(3)+u32(0)+u32(0)+i64(64*Q32)+i64(0)+z(24)
    p+=sec_header(6,1,64)
    p+=u32(1)+u32(5)+u32(1)+u32(2)+u32(0)+u32(0)+i64(24000)+i64(48000)+i64(0)+i64(Q32)+z(8)
    p+=sec_header(7,1,64)
    p+=u32(1)+u32(1)+u32(2)+u32(1)+i64(24000)+i64(30000)+u32(0)+z(28)
    p+=sec_header(8,1,64)
    p+=u32(1)+u32(1)+u32(2)+u32(1)+u32(2)+u32(0)+i64(24000)+z(32)
    p+=sec_header(9,1,64)
    p+=u32(1920)+u32(1080)+i32(30)+u32(48000)+i64(1600)+i64(48000)+i64(30)+i64(48000)+z(16)
    payload=bytes(p)
    pd=hashlib.sha256(payload).digest()
    h=bytearray(b'ODMC')+struct.pack('<HH',1,0)+u32(0x30524952)+u32(0)+struct.pack('<HH',1,1)+u32(0)+u64(len(payload))+pd
    assert len(h)==64
    return bytes(h)+payload,sh,ch

PROBE = r'''#include "odm_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void node(odm_score_node*n,unsigned id,unsigned scene,unsigned role,unsigned cap,unsigned resource,long long start,long long end,int z){memset(n,0,sizeof(*n));n->node_id=id;n->scene_id=scene;n->role=role;n->capability_id=cap;n->capability_major=1;n->resource_id=resource;n->state_model=ODM_STATELESS;n->z_index=z;n->start_sample=start;n->end_sample=end;n->scale_x_micro=ODM_MICRO_ONE;n->scale_y_micro=ODM_MICRO_ONE;n->opacity_micro=ODM_MICRO_ONE;n->seed_domain=(uint64_t)id*UINT64_C(0x9e3779b97f4a7c15);}
static void hx(const odm_sha256_digest*d){unsigned i;for(i=0;i<32;i++)printf("%02x",d->bytes[i]);puts("");}
int main(int argc,char**argv){odm_score_header h={0};odm_score_resource r[1]={{0}};odm_score_act a[1]={{0}};odm_score_scene s[2]={{0}};odm_score_node n[5];odm_score_cue q[1]={{0}};odm_score_modulator m[1]={{0}};odm_score_automation au[1]={{0}};odm_score_transition tr[1]={{0}};odm_score_state_transfer tf[1]={{0}};odm_score_view v={0};odm_music_map_binding music={0};odm_sha256_digest sh,ch,ih;uint64_t need=0;uint8_t*b;FILE*f;if(argc!=2)return 2;
h.schema_version_major=1;h.schema_version_minor=1;h.fps=30;h.width=1920;h.height=1080;h.project_end_sample=48000;h.project_seed=UINT64_C(0x123456789abcdef0);h.resource_count=1;h.act_count=1;h.scene_count=2;h.node_count=5;h.cue_count=1;h.modulator_count=1;h.automation_count=1;h.transition_count=1;h.transfer_count=1;
r[0].resource_id=1;r[0].kind=ODM_SCORE_RESOURCE_IMAGE;if(odm_sha256("image-one",9,&r[0].content_sha256))return 3;a[0].act_id=1;a[0].end_sample=48000;s[0].scene_id=1;s[0].act_id=1;s[0].environment_node_id=1;s[0].primary_node_id=2;s[0].end_sample=30000;s[1].scene_id=2;s[1].act_id=1;s[1].environment_node_id=3;s[1].primary_node_id=4;s[1].start_sample=24000;s[1].end_sample=48000;
node(&n[0],1,1,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,0,0,30000,-100);node(&n[1],2,1,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_EMPTY,0,0,30000,0);node(&n[2],3,2,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,0,24000,48000,-100);node(&n[3],4,2,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_IMAGE,1,24000,48000,0);node(&n[4],5,2,ODM_SCORE_NODE_EFFECT,ODM_CAP_RESIDUAL_EVENT_FIELD,0,24000,48000,10);n[4].state_model=ODM_EVENT_HISTORY;n[4].state_slot=1;n[4].opacity_micro=500000;
q[0].cue_id=1;q[0].scene_id=1;q[0].kind=ODM_SCORE_CUE_GENERIC;q[0].sample=1000;m[0].modulator_id=1;m[0].node_id=5;m[0].target_parameter=ODM_PARAM_OPACITY;m[0].source=ODM_MOD_RMS_MID;m[0].scale_micro=64000000;au[0].automation_id=1;au[0].node_id=5;au[0].target_parameter=ODM_PARAM_X;au[0].interpolation=ODM_AUTOMATION_LINEAR;au[0].start_sample=24000;au[0].end_sample=48000;au[0].to_micro=1000000;tr[0].transition_id=1;tr[0].from_scene_id=1;tr[0].to_scene_id=2;tr[0].kind=ODM_TRANSITION_CROSSFADE;tr[0].start_sample=24000;tr[0].end_sample=30000;tf[0].transfer_id=1;tf[0].from_scene_id=1;tf[0].to_scene_id=2;tf[0].state_slot=1;tf[0].mode=ODM_TRANSFER_RESET;tf[0].at_sample=24000;
v.header=&h;v.resources=r;v.acts=a;v.scenes=s;v.nodes=n;v.cues=q;v.modulators=m;v.automation=au;v.transitions=tr;v.transfers=tf;if(odm_score_hash(&v,&sh)||odm_score_capability_hash(&v,&ch))return 4;if(odm_sha256("music-map",9,&music.music_map_sha256)||odm_music_policy_current_sha256(&music.music_policy_sha256))return 5;music.canonical_frames=30000;if(odm_score_compile_render_ir(&v,&music,NULL,0,&need,NULL)!=ODM_STATUS_BUFFER_TOO_SMALL)return 6;b=malloc((size_t)need);if(!b)return 7;if(odm_score_compile_render_ir(&v,&music,b,need,&need,&ih))return 8;f=fopen(argv[1],"wb");if(!f)return 9;if(fwrite(b,1,(size_t)need,f)!=(size_t)need)return 10;fclose(f);printf("bytes=%llu\n",(unsigned long long)need);hx(&sh);hx(&ch);hx(&music.music_policy_sha256);hx(&ih);free(b);return 0;}
'''

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);args=ap.parse_args()
    root=pathlib.Path(args.root).resolve();lib=pathlib.Path(args.library).resolve()
    expected,esh,ech=expected_record()
    with tempfile.TemporaryDirectory(prefix='odm_g4_oracle_') as td:
        td=pathlib.Path(td);c=td/'probe.c';exe=td/'probe';out=td/'render.ir';c.write_text(PROBE)
        cmd=[args.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(c),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        subprocess.run(cmd,check=True,cwd=root)
        run=subprocess.run([str(exe),str(out)],check=True,cwd=root,text=True,capture_output=True)
        lines=[x.strip() for x in run.stdout.splitlines() if x.strip()]
        got=out.read_bytes()
    if got!=expected:
        n=min(len(got),len(expected));idx=next((i for i in range(n) if got[i]!=expected[i]),n)
        raise SystemExit(f'Render IR oracle mismatch at byte {idx}: got_len={len(got)} expected_len={len(expected)} got={got[idx:idx+16].hex()} expected={expected[idx:idx+16].hex()}')
    if len(lines)<5: raise SystemExit('probe output incomplete')
    if bytes.fromhex(lines[1])!=esh: raise SystemExit('Score SHA mismatch')
    if bytes.fromhex(lines[2])!=ech: raise SystemExit('capability SHA mismatch')
    if bytes.fromhex(lines[3])!=POLICY_SHA: raise SystemExit('music policy SHA drift')
    irsha=hashlib.sha256(expected).hexdigest()
    if lines[4]!=irsha: raise SystemExit('IR SHA mismatch')
    print(f'Render IR oracle: pass ({len(expected)} bytes exact)')
    print(f'Score SHA-256: {esh.hex()}')
    print(f'Capability SHA-256: {ech.hex()}')
    print(f'Music policy SHA-256: {POLICY_SHA.hex()}')
    print(f'Render IR SHA-256: {irsha}')

if __name__=='__main__': main()
