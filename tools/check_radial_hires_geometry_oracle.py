#!/usr/bin/env python3
"""Independent pixel-exact oracle for ODPAR high-resolution 96-radial field lines."""
from __future__ import annotations
import argparse, hashlib, math, pathlib, struct, subprocess, tempfile

Q=2147483647; MASK32=(1<<32)-1; W=H=64; QUARTER=0x40000000; K=1304065748
ATAN=[536870912,316933406,167458907,85004756,42667331,21354465,10679838,5340245,2670163,1335087,667544,333772,166886,83443,41722,20861,10430,5215,2608,1304,652,326,163,81,41,20,10,5,3,1,1]
RING_PHASE=0x2468ACE1
PRIMARY=(62000,18000,9000,65535); SECONDARY=(10000,22000,61000,65535)
PROBE=r'''
#include "odm_compositor.h"
#include "compositor_internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t q(uint32_t n,uint32_t d){return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);}
int main(int ac,char**av){
 odm_layered_config c;odm_composition_frame_state co;odm_director_frame_state di;odm_layered_frame_plan p;odm_layered_pixel16 *f;FILE*out;uint32_t i;
 if(ac!=2)return 2;
 memset(&c,0,sizeof(c));
 memset(&co,0,sizeof(co));
 memset(&di,0,sizeof(di));
 c.schema_version=1;c.canvas.schema_version=1;c.canvas.aspect=ODM_CANVAS_ASPECT_SQUARE_1_1;c.canvas.width=64;c.canvas.height=64;c.canvas.fps=30;
 c.background.schema_version=1;c.background.style=ODM_BACKGROUND_NONE;c.background.opacity_q31=INT32_MAX;
 c.core.schema_version=1;c.core.shape=ODM_CORE_SHAPE_CIRCLE;c.core.fit=ODM_CORE_FIT_STRETCH;c.core.center_x_q31=q(1,2);c.core.center_y_q31=q(1,2);c.core.width_q31=q(1,4);c.core.height_q31=q(1,4);c.core.feather_q16=1u<<16;c.core.opacity_q31=INT32_MAX;
 c.field.schema_version=1;c.field.flags=ODM_FIELD_RADIAL_BARS;c.field.radial_segments=96;c.field.ring_gap_q16=1u<<16;c.field.bar_min_q16=0;c.field.bar_max_q16=4u<<16;c.field.bar_width_q16=1u<<16;c.field.particle_radius_q16=1u<<16;c.field.field_opacity_q31=q(3,4);c.field.primary_color.r=62000;c.field.primary_color.g=18000;c.field.primary_color.b=9000;c.field.primary_color.a=65535;c.field.secondary_color.r=10000;c.field.secondary_color.g=22000;c.field.secondary_color.b=61000;c.field.secondary_color.a=65535;
 c.hud.schema_version=1;c.hud.text_scale_q16=1u<<16;c.hud.opacity_q31=INT32_MAX;
 co.schema_version=ODM_COMPOSITION_SCHEMA_VERSION;co.mode=ODM_COMPOSITION_MODE_FLOW;co.flags=ODM_COMPOSITION_FLAG_RADIAL_HIRES;co.tick_index=123;co.center_sample=59040;co.ring_phase=UINT32_C(0x2468ace1);co.radial_gain_q31=INT32_MAX;for(i=0;i<96;i++)co.radial_q31[i]=(uint32_t)(((uint64_t)INT32_MAX*((i*17u+5u)%97u))/96u);
 di.schema_version=ODM_DIRECTOR_SCHEMA_VERSION;di.tick_index=co.tick_index;di.layout=ODM_DIRECTOR_LAYOUT_ORBIT;
 if(odm_layered_config_validate(&c)!=ODM_STATUS_OK)return 3;
 if(odm_layered_resolve_frame_plan(&c,&co,&di,59040,96000,&p)!=ODM_STATUS_OK)return 4;
 f=(odm_layered_pixel16*)calloc((size_t)64u*64u,sizeof(*f));if(!f)return 5;odm_layered_field_render(&c,&p,f);out=fopen(av[1],"wb");if(!out){free(f);return 6;}if(fwrite(f,sizeof(*f),64u*64u,out)!=64u*64u){fclose(out);free(f);return 7;}if(fclose(out)!=0){free(f);return 8;}free(f);return 0;
}
'''

def q(n,d):return (Q*n+d//2)//d
def cdiv(n,d):return n//d if n>=0 else -((-n)//d)
def div_round_away(n,d):return (n+d//2)//d if n>=0 else -(((-n)+d//2)//d)
def ratio_px(dim,v):return (dim*65536*v+Q//2)//Q
def sinq(ph):
 ph&=MASK32;quad=ph>>30;off=ph&0x3fffffff;sign=-1 if quad>=2 else 1;z=off if quad in (0,2) else QUARTER-off;x=K;y=0
 if ph in (0,0x80000000):return 0
 if ph==QUARTER:return Q
 if ph==0xc0000000:return -Q
 for i,a in enumerate(ATAN):
  d=1<<i;xs=cdiv(x,d);ys=cdiv(y,d)
  if z>=0:x,y,z=x-ys,y+xs,z-a
  else:x,y,z=x+ys,y-xs,z+a
 y=y if sign>0 else -y
 return max(-Q,min(Q,y))
def cosq(ph):return sinq((ph+QUARTER)&MASK32)
def m16q(v,f):return min(65535,(v*f+Q//2)//Q)
def m16(a,b):return (a*b+32767)//65535
def blend(dst,src,cov,op):
 if not cov or not op:return dst
 qq=(cov*op+Q//2)//Q;a=m16q(src[3],qq)
 if not a:return dst
 sp=(m16(src[0],a),m16(src[1],a),m16(src[2],a),a);inv=65535-a
 return tuple(min(65535,sv+(dv*inv+32767)//65535) for sv,dv in zip(sp,dst))
def coverage(distance,half,feather=65536):
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
 center=ratio_px(64,q(1,2));base=ratio_px(64,q(1,4));radius=base//2;gap=1<<16;op=q(3,4);frame=[(0,0,0,0) for _ in range(W*H)]
 radial=[(Q*((i*17+5)%97))//96 for i in range(96)]
 for i,rv in enumerate(radial):
  ph=(RING_PHASE+((i<<32)//96))&MASK32;cs=cosq(ph);sn=sinq(ph);span=4<<16;ln=(span*rv+Q//2)//Q;r0=radius+gap;r1=r0+ln
  ax=center+cdiv(r0*cs,Q);ay=center+cdiv(r0*sn,Q);bx=center+cdiv(r1*cs,Q);by=center+cdiv(r1*sn,Q);draw_segment(frame,ax,ay,bx,by,1<<16,SECONDARY if i&1 else PRIMARY,op)
 return b''.join(struct.pack('<HHHH',*p) for p in frame)

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args();root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
 with tempfile.TemporaryDirectory(prefix='odm-radial-hires-oracle-') as td0:
  td=pathlib.Path(td0);src=td/'p.c';exe=td/'p';raw=td/'f.bin';src.write_text(PROBE+'\n');cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),'-I',str(root/'src/compositor'),'-I',str(root/'src/renderer'),'-I',str(root/'src/ir'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)];r=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
  if r.returncode:raise SystemExit('radial probe compile failed\n'+r.stdout+r.stderr)
  r=subprocess.run([str(exe),str(raw)],cwd=root,capture_output=True,text=True)
  if r.returncode:raise SystemExit(f'radial probe failed rc={r.returncode}\n{r.stdout}\n{r.stderr}')
  got=raw.read_bytes()
 exp=expected()
 if got!=exp:
  j=next(i for i,(x,y) in enumerate(zip(got,exp)) if x!=y);pix=j//8;raise SystemExit(f'radial raster mismatch byte={j} pixel=({pix%W},{pix//W}) got={got[pix*8:pix*8+8].hex()} exp={exp[pix*8:pix*8+8].hex()}')
 print('RADIAL HIRES GEOMETRY ORACLE: PASS');print(f'  segments: 96, pixels={W*H}, channels={W*H*4}');print('  independently reconstructed: CORDIC + endpoints + Q24.8 projection + coverage + premul blend');print('  stream sha256='+hashlib.sha256(exp).hexdigest());return 0
if __name__=='__main__':raise SystemExit(main())
