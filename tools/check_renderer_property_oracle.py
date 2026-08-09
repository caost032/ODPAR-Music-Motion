#!/usr/bin/env python3
"""Independent property sweep for Gate-6 reference pixels."""
from __future__ import annotations
import argparse, hashlib, pathlib, random, subprocess, tempfile
import check_renderer_oracle as ro

CASES=64
SURFACE=bytes([
 255,0,0,255,   0,255,0,192,   0,0,255,128,   255,255,255,0,
 255,128,0,224, 0,255,255,160, 255,0,255,96,   32,64,96,255,
 10,200,40,48,  200,20,180,255, 80,100,240,200, 240,220,20,80,
 15,30,45,255,  120,60,30,180,  210,150,90,120,  250,250,250,255])
SOURCE_SHA=hashlib.sha256(b'property-source').digest()

def micro_q32(v:int)->int:
    whole=ro.div_trunc(v,1_000_000);rem=v-whole*1_000_000
    return whole*ro.Q32+ro.div_round_away(rem*ro.Q32,1_000_000)
def phase_micro(v:int)->int:
    n=v%1_000_000;num=n*(1<<32);q,r=divmod(num,1_000_000)
    if r and r>=1_000_000-r:q+=1
    return q&0xffffffff

def make_cases():
    r=random.Random(0x4f44504152)
    out=[]
    for i in range(CASES):
        x=r.randint(-700_000,700_000);y=r.randint(-700_000,700_000)
        sx=r.randint(220_000,1_500_000);sy=r.randint(220_000,1_500_000)
        op=r.randint(80_000,1_000_000);phase=r.randrange(0,1_000_000)
        transfer=ro.TRANSFER_SRGB if (i&1)==0 else ro.TRANSFER_LINEAR
        out.append((x,y,sx,sy,op,phase,transfer))
    # Explicit edge-like cases mixed into random sweep.
    out[0]=(0,0,1_000_000,1_000_000,1_000_000,0,ro.TRANSFER_SRGB)
    out[1]=(0,0,500_000,500_000,500_000,125_000,ro.TRANSFER_LINEAR)
    out[2]=(500_000,-500_000,1_250_000,375_000,333_333,333_333,ro.TRANSFER_SRGB)
    return out

def expected(case):
    x,y,sx,sy,op,phase,transfer=case
    xq,yq=micro_q32(x),micro_q32(y);sxq,syq=micro_q32(sx),micro_q32(sy);oq=ro.q1_ratio(op,1_000_000);ph=phase_micro(phase)
    b=bytearray()
    for py in range(ro.HEIGHT):
      for px in range(ro.WIDTH):
        dst=(0,0,0,ro.I32_MAX)
        src=ro.sample_surface(SURFACE,4,4,transfer,px,py,sxq,syq,oq,ph,xq,yq)
        p=ro.blend_over(dst,src)
        import struct
        b+=struct.pack('<HHHH',ro.q27_to_u16(p[0]),ro.q27_to_u16(p[1]),ro.q27_to_u16(p[2]),ro.q31_to_u16(p[3]))
    return bytes(b)

def c_source(cases):
    rows=',\n'.join('{%d,%d,%d,%d,%d,%d,%d}'%c for c in cases)
    surf=','.join(str(x) for x in SURFACE)
    return r'''#include "odm_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct{int64_t x,y,sx,sy,op,phase;uint32_t transfer;} case_t;
static const case_t cases[]={''' + "\n" + rows + r'''};
static const uint8_t pixels[64]={'''+surf+r'''};
static void hx(const uint8_t*p,size_t n){size_t i;for(i=0;i<n;i++)printf("%02x",p[i]);}
static void node(odm_score_node*n,uint32_t id,uint32_t role,uint32_t cap,uint32_t res){memset(n,0,sizeof(*n));n->node_id=id;n->scene_id=1;n->role=role;n->capability_id=cap;n->capability_major=1;n->resource_id=res;n->state_model=ODM_STATELESS;n->z_index=role==ODM_SCORE_NODE_ENVIRONMENT?-100:0;n->start_sample=0;n->end_sample=1600;n->scale_x_micro=ODM_MICRO_ONE;n->scale_y_micro=ODM_MICRO_ONE;n->opacity_micro=ODM_MICRO_ONE;n->seed_domain=id;}
int main(void){size_t ci;for(ci=0;ci<sizeof(cases)/sizeof(cases[0]);ci++){const case_t*c=&cases[ci];odm_score_header h={0};odm_score_resource r={0};odm_score_act a={0};odm_score_scene sc={0};odm_score_node n[2];odm_score_view score={0};odm_music_map_binding bind={0};uint64_t ib=0,fb=0,sb=0;uint8_t*ir,*scratch,frame[512];odm_render_surface_frame sf={0};odm_render_resource_view rv={0};odm_render_asset_set assets={0};odm_reference_frame_info fi;
h.schema_version_major=ODM_SCORE_SCHEMA_MAJOR;h.schema_version_minor=ODM_SCORE_SCHEMA_MINOR;h.fps=30;h.width=8;h.height=8;h.project_end_sample=1600;h.project_seed=(uint64_t)ci;h.resource_count=1;h.act_count=1;h.scene_count=1;h.node_count=2;r.resource_id=1;r.kind=ODM_SCORE_RESOURCE_IMAGE;if(odm_sha256("property-source",15,&r.content_sha256))return 2;a.act_id=1;a.end_sample=1600;sc.scene_id=1;sc.act_id=1;sc.environment_node_id=1;sc.primary_node_id=2;sc.end_sample=1600;node(&n[0],1,ODM_SCORE_NODE_ENVIRONMENT,ODM_CAP_BLACK_VOID,0);node(&n[1],2,ODM_SCORE_NODE_PRIMARY,ODM_CAP_PRIMARY_IMAGE,1);n[1].x_micro=c->x;n[1].y_micro=c->y;n[1].scale_x_micro=c->sx;n[1].scale_y_micro=c->sy;n[1].opacity_micro=c->op;n[1].phase_microturns=c->phase;score.header=&h;score.resources=&r;score.acts=&a;score.scenes=&sc;score.nodes=n;bind.canonical_frames=1600;if(odm_sha256("property-map",12,&bind.music_map_sha256)||odm_music_policy_current_sha256(&bind.music_policy_sha256))return 3;if(odm_score_compile_render_ir(&score,&bind,NULL,0,&ib,NULL)!=ODM_STATUS_BUFFER_TOO_SMALL)return 4;ir=malloc((size_t)ib);if(!ir)return 5;if(odm_score_compile_render_ir(&score,&bind,ir,ib,&ib,NULL))return 6;sf.width=4;sf.height=4;sf.pixel_format=ODM_RENDER_SURFACE_RGBA8;sf.primaries=ODM_COLOR_PRIMARIES_BT709;sf.transfer=c->transfer;sf.alpha_mode=ODM_ALPHA_STRAIGHT;sf.pixel_bytes=64;sf.pixels=pixels;rv.resource_id=1;rv.kind=ODM_SCORE_RESOURCE_IMAGE;rv.frame_count=1;rv.source_sha256=r.content_sha256;rv.frames=&sf;assets.resource_count=1;assets.resources=&rv;if(odm_reference_render_requirements(ir,ib,0,NULL,&fb,&sb)||fb!=512)return 7;scratch=malloc((size_t)sb);if(!scratch)return 8;if(odm_reference_render_frame(ir,ib,0,NULL,&assets,NULL,scratch,sb,frame,512,&fb,&sb,&fi))return 9;printf("C,%zu,",ci);hx(fi.pixel_sha256.bytes,32);putchar(',');hx(frame,512);putchar('\n');free(scratch);free(ir);}return 0;}
'''

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args();root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve();cases=make_cases()
    with tempfile.TemporaryDirectory(prefix='odm_render_prop_') as td:
      td=pathlib.Path(td);c=td/'p.c';e=td/'p';c.write_text(c_source(cases) + '\n');subprocess.run([a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(c),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(e)],cwd=root,check=True);run=subprocess.run([str(e)],cwd=root,check=True,text=True,capture_output=True)
    lines=[x.split(',') for x in run.stdout.splitlines() if x.strip()]
    if len(lines)!=CASES:raise SystemExit(f'property probe produced {len(lines)} cases, expected {CASES}')
    for i,(case,row) in enumerate(zip(cases,lines)):
      if row[0]!='C' or int(row[1])!=i:raise SystemExit(f'case ordering mismatch at {i}')
      exp=expected(case);got=bytes.fromhex(row[3]);
      if got!=exp:
        j=next((k for k,(x,y) in enumerate(zip(got,exp)) if x!=y),min(len(got),len(exp)));raise SystemExit(f'property renderer mismatch case={i} byte={j} case_values={case} got={got[j:j+16].hex()} expected={exp[j:j+16].hex()}')
      if bytes.fromhex(row[2])!=hashlib.sha256(exp).digest():raise SystemExit(f'property pixel SHA mismatch case={i}')
    print(f'Renderer property oracle: pass ({CASES} transforms, {CASES*ro.WIDTH*ro.HEIGHT} pixels / {CASES*ro.WIDTH*ro.HEIGHT*4} channels exact)')
if __name__=='__main__':main()
