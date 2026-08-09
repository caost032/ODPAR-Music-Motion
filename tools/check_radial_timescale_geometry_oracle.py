#!/usr/bin/env python3
"""Independent pixel-exact oracle for multiscale causal radial morphology.

A single strict-causal lane is rendered end-to-end through the public
Music-Reaction -> Composition -> Layered Plan -> Field stack. Python independently
reconstructs body, slow-fast release segment, and transient tip geometry/blending.
"""
from __future__ import annotations
import argparse, hashlib, math, pathlib, struct, subprocess, tempfile

Q=2147483647
W=H=64
C045=966367642
C035=751619277
PRIMARY=(62000,18000,9000,65535)
SECONDARY=(10000,22000,61000,65535)
SLOW=1800000000
FAST=900000000
ATTACK=800000000
FIELD_OP=Q
BAR_MAX=12<<16
BAR_WIDTH=1<<16
RING_GAP=1<<16

PROBE=r'''#include "odm_compositor.h"
#include "odm_music_reaction.h"
#include "odm_visual.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t q(uint32_t n,uint32_t d){return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);}
int main(int ac,char**av){
 odm_layered_config c; odm_music_analysis_tick b; odm_music_reaction_frame r;
 odm_composition_frame_state co; odm_director_frame_state di; odm_layered_frame_plan p;
 uint64_t fb=0,sb=0,fr=0,sr=0; void *scratch=NULL; uint8_t *frame=NULL; FILE*out; odm_sha256_digest h;
 if(ac!=2)return 2;
 memset(&c,0,sizeof(c));memset(&b,0,sizeof(b));memset(&r,0,sizeof(r));memset(&di,0,sizeof(di));
 c.schema_version=ODM_LAYERED_SCHEMA_VERSION;c.canvas.schema_version=1u;c.canvas.aspect=ODM_CANVAS_ASPECT_SQUARE_1_1;c.canvas.width=64;c.canvas.height=64;c.canvas.fps=30;
 c.background.schema_version=1u;c.background.style=ODM_BACKGROUND_NONE;c.background.opacity_q31=INT32_MAX;
 c.core.schema_version=1u;c.core.shape=ODM_CORE_SHAPE_CIRCLE;c.core.fit=ODM_CORE_FIT_STRETCH;c.core.center_x_q31=q(1,2);c.core.center_y_q31=q(1,2);c.core.width_q31=q(1,4);c.core.height_q31=q(1,4);c.core.feather_q16=1u<<16;c.core.opacity_q31=INT32_MAX;c.core.scale_reactivity_q31=0u;
 c.field.schema_version=1u;c.field.flags=ODM_FIELD_RADIAL_BARS;c.field.radial_segments=96;c.field.particle_count=0u;c.field.ring_gap_q16=1u<<16;c.field.bar_min_q16=0;c.field.bar_max_q16=12u<<16;c.field.bar_width_q16=1u<<16;c.field.particle_radius_q16=1u<<16;c.field.field_opacity_q31=INT32_MAX;
 c.field.primary_color.r=62000;c.field.primary_color.g=18000;c.field.primary_color.b=9000;c.field.primary_color.a=65535;c.field.secondary_color.r=10000;c.field.secondary_color.g=22000;c.field.secondary_color.b=61000;c.field.secondary_color.a=65535;
 c.hud.schema_version=1u;c.hud.text_scale_q16=1u<<16;c.hud.opacity_q31=INT32_MAX;
 if(odm_layered_config_validate(&c)!=ODM_STATUS_OK)return 3;
 b.tick_index=100u;b.center_sample=UINT64_C(48000);b.silence=0u;
 r.schema_version=ODM_MUSIC_REACTION_SCHEMA_VERSION;r.tick_index=b.tick_index;r.center_sample=b.center_sample;
 r.lane_q31[0]=UINT32_C(1800000000);r.lane_fast_q31[0]=UINT32_C(900000000);r.lane_attack_q31[0]=UINT32_C(800000000);
 if(odm_composition_resolve_music_reaction(&b,&r,&co)!=ODM_STATUS_OK)return 4;
 di.schema_version=ODM_DIRECTOR_SCHEMA_VERSION;di.tick_index=co.tick_index;di.layout=ODM_DIRECTOR_LAYOUT_MONOLITH;
 if(odm_layered_resolve_frame_plan(&c,&co,&di,48000,96000,&p)!=ODM_STATUS_OK)return 5;
 printf("M,%u,%u,%u,%u,%u\n",p.flags,p.radial_q31[0],p.radial_body_q31[0],p.radial_release_q31[0],p.radial_attack_q31[0]);
 if(odm_layered_render_requirements(&c,ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,&fb,&sb)!=ODM_STATUS_OK)return 6;
 scratch=malloc((size_t)sb);frame=(uint8_t*)malloc((size_t)fb);if(!scratch||!frame){free(scratch);free(frame);return 7;}
 if(odm_layered_render_stack_rgba16(&c,&p,NULL,ODM_LAYERED_LAYER_MASK_FIELD,NULL,scratch,sb,frame,fb,&fr,&sr,&h)!=ODM_STATUS_OK){free(scratch);free(frame);return 8;}
 if(fr!=fb||sr!=sb){free(scratch);free(frame);return 9;}
 out=fopen(av[1],"wb");if(!out){free(scratch);free(frame);return 10;}if(fwrite(frame,1,(size_t)fb,out)!=(size_t)fb){fclose(out);free(scratch);free(frame);return 11;}if(fclose(out)!=0){free(scratch);free(frame);return 12;}free(scratch);free(frame);return 0;
}
'''

def mulq(a:int,b:int)->int:
    return min(Q,(a*b+Q//2)//Q)

def morphology():
    body=mulq(FAST,C045)
    release=SLOW-FAST
    release_mix=mulq(release,C035)
    after=body+mulq(Q-body,release_mix)
    final=after+mulq(Q-after,ATTACK)
    return final,body,release,after

def q(n:int,d:int)->int:return (Q*n+d//2)//d
def ratio_px(dim:int,v:int)->int:return (dim*65536*v+Q//2)//Q
def div_round_away(n:int,d:int)->int:return (n+d//2)//d if n>=0 else -(((-n)+d//2)//d)
def m16q(v:int,f:int)->int:return min(65535,(v*f+Q//2)//Q)
def m16(a:int,b:int)->int:return (a*b+32767)//65535

def blend(dst,src,cov,op):
    if not cov or not op:return dst
    qq=(cov*op+Q//2)//Q;a=m16q(src[3],qq)
    if not a:return dst
    sp=(m16(src[0],a),m16(src[1],a),m16(src[2],a),a);inv=65535-a
    return tuple(min(65535,sv+(dv*inv+32767)//65535) for sv,dv in zip(sp,dst))

def coverage(distance:int,half:int,feather:int=65536)->int:
    distance=abs(distance);half=max(0,half)
    if feather<=0:return Q if distance<=half else 0
    if distance<=half:return Q
    d=distance-half
    if d>=feather:return 0
    return ((feather-d)*Q+feather//2)//feather

def draw_disk(frame,cx,cy,radius,col,op):
    feather=65536;minx=(cx-radius-feather)>>16;maxx=(cx+radius+feather)>>16;miny=(cy-radius-feather)>>16;maxy=(cy+radius+feather)>>16
    minx=max(0,minx);miny=max(0,miny);maxx=min(W-1,maxx);maxy=min(H-1,maxy)
    for y in range(miny,maxy+1):
        for x in range(minx,maxx+1):
            dx=((x<<16)+32768)-cx;dy=((y<<16)+32768)-cy;dist=math.isqrt(abs(dx)*abs(dx)+abs(dy)*abs(dy));sdf=dist-radius
            if sdf<=-(feather//2):cov=Q
            elif sdf>=feather//2:cov=0
            else:cov=((feather//2-sdf)*Q+feather//2)//feather
            if cov:
                j=y*W+x;frame[j]=blend(frame[j],col,cov,op)

def draw_segment(frame,axq,ayq,bxq,byq,width,col,op):
    ax=div_round_away(axq,256);ay=div_round_away(ayq,256);bx=div_round_away(bxq,256);by=div_round_away(byq,256);vx=bx-ax;vy=by-ay;len2=vx*vx+vy*vy;rad=width//2+65536
    minx=(min(axq,bxq)-rad)>>16;maxx=(max(axq,bxq)+rad)>>16;miny=(min(ayq,byq)-rad)>>16;maxy=(max(ayq,byq)+rad)>>16
    minx=max(0,minx);miny=max(0,miny);maxx=min(W-1,maxx);maxy=min(H-1,maxy)
    if len2<=0:
        draw_disk(frame,axq,ayq,width//2,col,op);return
    for y in range(miny,maxy+1):
        for x in range(minx,maxx+1):
            px=(x<<8)+128;py=(y<<8)+128;dot=(px-ax)*vx+(py-ay)*vy
            if dot<=0:t=0
            elif dot>=len2:t=65536
            else:t=(dot*65536+len2//2)//len2
            cx=ax+div_round_away(vx*t,65536);cy=ay+div_round_away(vy*t,65536);dx=px-cx;dy=py-cy;dist=math.isqrt(dx*dx+dy*dy)<<8;cov=coverage(dist,width//2)
            if cov:
                j=y*W+x;frame[j]=blend(frame[j],col,cov,op)

def expected():
    final,body,release,after=morphology()
    center=ratio_px(64,q(1,2));base=ratio_px(64,q(1,4));radius=base//2;r0=radius+RING_GAP
    body_len=(BAR_MAX*body+Q//2)//Q
    release_len=(BAR_MAX*after+Q//2)//Q
    final_len=(BAR_MAX*final+Q//2)//Q
    ax=center+r0;ay=center
    bx=center+r0+body_len;by=center
    rx=center+r0+release_len;ry=center
    tx=center+r0+final_len;ty=center
    frame=[(0,0,0,0) for _ in range(W*H)]
    if body_len>0:draw_segment(frame,ax,ay,bx,by,BAR_WIDTH,SECONDARY,FIELD_OP)
    if release_len>body_len and release:
        draw_segment(frame,bx,by,rx,ry,BAR_WIDTH,SECONDARY,mulq(FIELD_OP,release))
    if final_len>release_len and ATTACK:
        draw_segment(frame,rx,ry,tx,ty,BAR_WIDTH,PRIMARY,mulq(FIELD_OP,ATTACK))
    raw=b''.join(struct.pack('<HHHH',*p) for p in frame)
    return raw,(final,body,release,ATTACK),(body_len,release_len,final_len)

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args();root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-timescale-radial-') as td0:
        td=pathlib.Path(td0);src=td/'probe.c';exe=td/'probe';raw=td/'frame.bin';src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        c=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if c.returncode:raise SystemExit('timescale radial probe compile failed\n'+c.stdout+c.stderr)
        x=subprocess.run([str(exe),str(raw)],cwd=root,capture_output=True,text=True)
        if x.returncode:raise SystemExit(f'timescale radial probe failed rc={x.returncode}\n{x.stdout}\n{x.stderr}')
        got=raw.read_bytes(); meta=[ln for ln in x.stdout.splitlines() if ln.startswith('M,')]
    exp,tuple_exp,lengths=expected()
    if len(meta)!=1:raise SystemExit(f'missing metadata: {meta}')
    vals=tuple(map(int,meta[0].split(',')[1:]))
    if vals[0] != (4|8|16|32):raise SystemExit(f'flags mismatch {vals[0]}')
    if vals[1:] != tuple_exp:raise SystemExit(f'morphology mismatch got={vals[1:]} expected={tuple_exp}')
    if got!=exp:
        n=min(len(got),len(exp));j=next((i for i in range(n) if got[i]!=exp[i]),n);pix=j//8
        raise SystemExit(f'timescale radial raster mismatch byte={j} pixel=({pix%W},{pix//W}) got={got[pix*8:pix*8+8].hex()} exp={exp[pix*8:pix*8+8].hex()}')
    print('RADIAL TIMESCALE GEOMETRY ORACLE: PASS')
    print(f'  final/body/release/attack={tuple_exp}')
    print(f'  body/release/final lengths q16={lengths}')
    print('  independently reconstructed: body segment + slow-fast release segment + attack tip + exact premul blend')
    print('  stream sha256='+hashlib.sha256(exp).hexdigest())
    return 0
if __name__=='__main__':raise SystemExit(main())
