#!/usr/bin/env python3
"""Independent pixel oracle for STRICT_CAUSAL particle geometry.

Reconstructs the strict particle topology and its music-authoritative radial
motion without calling the C implementation for expected values. It also proves
that sample/tick/ring phase/config seed cannot move a held strict field, while a
single spectral-radial change does change the raster.
"""
from __future__ import annotations
import argparse, hashlib, math, pathlib, struct, subprocess, tempfile

Q=2147483647
MASK32=(1<<32)-1
MASK64=(1<<64)-1
W=H=64
SEG=96
PARTICLES=96
PRIMARY=(50000,30000,10000,60000)
ATAN=[536870912,316933406,167458907,85004756,42667331,21354465,10679838,5340245,2670163,1335087,667544,333772,166886,83443,41722,20861,10430,5215,2608,1304,652,326,163,81,41,20,10,5,3,1,1]
CORDIC_K=1304065748
QUARTER=0x40000000

PROBE=r'''#include "odm_compositor.h"
#include "compositor_internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t qr(uint32_t n,uint32_t d){return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);}
static void init(odm_layered_config*c,odm_layered_frame_plan*p,uint32_t variant){
  uint32_t i;memset(c,0,sizeof(*c));memset(p,0,sizeof(*p));
  c->core.shape=ODM_CORE_SHAPE_CIRCLE;
  c->field.flags=ODM_FIELD_PARTICLES;c->field.radial_segments=96;c->field.particle_count=96;
  c->field.ring_gap_q16=1u<<16;c->field.particle_radius_q16=1u<<16;c->field.field_opacity_q31=qr(3,4);
  c->field.primary_color.r=50000;c->field.primary_color.g=30000;c->field.primary_color.b=10000;c->field.primary_color.a=60000;
  c->field.seed=variant==0?UINT64_C(1):UINT64_C(0xfedcba9876543210);
  p->width=64;p->height=64;p->center_x_q16=32<<16;p->center_y_q16=32<<16;
  p->core_radius_q16=8<<16;p->core_half_w_q16=8<<16;p->core_half_h_q16=8<<16;
  p->ring_inner_radius_q16=9<<16;p->field_opacity_q31=qr(3,4);p->particle_count=96;
  p->flags=ODM_COMPOSITION_FLAG_RADIAL_HIRES|ODM_COMPOSITION_FLAG_STRICT_CAUSAL;
  p->sample=variant==0?0:(variant==1?INT64_C(1234567):INT64_MAX-INT64_C(4096));
  p->tick_index=variant==0?0:(variant==1?UINT64_C(2572):UINT64_C(999999999));
  p->ring_phase=variant==0?0:UINT32_C(0xa5a5f00d);
  for(i=0;i<96;i++)p->radial_q31[i]=qr((i%12u)+1u,12u);
  if(variant==3)p->radial_q31[40]=(uint32_t)INT32_MAX;
}
int main(int argc,char**argv){
  odm_layered_config c;odm_layered_frame_plan p;odm_layered_pixel16*frame;FILE*f;uint32_t v;
  if(argc!=2)return 2;
  f=fopen(argv[1],"wb");
  if(!f)return 3;
  frame=(odm_layered_pixel16*)calloc(64u*64u,sizeof(*frame));if(!frame){fclose(f);return 4;}
  for(v=0;v<4;v++){init(&c,&p,v);memset(frame,0,64u*64u*sizeof(*frame));odm_layered_field_render(&c,&p,frame);
    if(fwrite(frame,sizeof(*frame),64u*64u,f)!=64u*64u){free(frame);fclose(f);return 5;}}
  free(frame);return fclose(f)!=0?6:0;
}
'''

def qratio(n,d):return (Q*n+d//2)//d
def cdiv(n,d):return n//d if n>=0 else -((-n)//d)
def mix64(x):
    x=(x+0x9E3779B97F4A7C15)&MASK64
    x=((x^(x>>30))*0xBF58476D1CE4E5B9)&MASK64
    x=((x^(x>>27))*0x94D049BB133111EB)&MASK64
    return (x^(x>>31))&MASK64

def sin_q31(phase):
    phase&=MASK32;q=phase>>30;off=phase&0x3fffffff;sign=-1 if q>=2 else 1;z=off if q in (0,2) else QUARTER-off;x,y=CORDIC_K,0
    if phase in (0,0x80000000):return 0
    if phase==QUARTER:return Q
    if phase==0xc0000000:return -Q
    for i,at in enumerate(ATAN):
        d=1<<i;xs,ys=cdiv(x,d),cdiv(y,d)
        if z>=0:x,y,z=x-ys,y+xs,z-at
        else:x,y,z=x+ys,y-xs,z+at
    if sign<0:y=-y
    return max(-Q,min(Q,y))
def cos_q31(p):return sin_q31((p+QUARTER)&MASK32)
def mul_u16_q31(v,q):return min(65535,(v*q+Q//2)//Q)
def mul_u16_u16(a,b):return (a*b+32767)//65535
def blend(dst,src,cov,opacity):
    if cov==0 or opacity==0:return dst
    q=(cov*opacity+Q//2)//Q;a=mul_u16_q31(src[3],q)
    if a==0:return dst
    prem=[mul_u16_u16(src[k],a) for k in range(3)]+[a];inv=65535-a
    return tuple(min(65535,prem[k]+(dst[k]*inv+32767)//65535) for k in range(4))
def coverage(dist,r):
    feather=65536;sdf=dist-r
    if sdf<=-(feather//2):return Q
    if sdf>=feather//2:return 0
    return ((feather//2-sdf)*Q+feather//2)//feather

def expected(changed=False):
    center=32<<16;core=8<<16;gap=1<<16;maxr=(64*65536*48)//100//2;rng=maxr-(9<<16);opacity=qratio(3,4)
    amps=[qratio((i%12)+1,12) for i in range(96)]
    if changed:amps[40]=Q
    frame=[(0,0,0,0) for _ in range(W*H)]
    for i in range(PARTICLES):
        h=mix64(0x51A7C0DE9E3779B9 ^ ((i*0x9E3779B97F4A7C15)&MASK64))
        sector=(i*73)%SEG;cell=(1<<32)//SEG;base=(sector*(1<<32))//SEG;jitter=((h>>48)&0xffff)*cell//65536;phase=(base+jitter)&MASK32
        amp=amps[sector]
        if amp==0:continue
        rq=mix64(h^0x5BF03635)>>33;cs,sn=cos_q31(phase),sin_q31(phase);boundary=core+gap
        baseq=536870912+rq//4;musicq=amp//2;radiusq=min(Q,baseq+musicq)
        r=boundary+(rng*radiusq+Q//2)//Q
        cx=center+cdiv(r*cs,Q);cy=center+cdiv(r*sn,Q);op=(opacity*amp+Q//2)//Q;rad=1<<16
        minx=max(0,(cx-rad-65536)>>16);maxx=min(W-1,(cx+rad+65536)>>16);miny=max(0,(cy-rad-65536)>>16);maxy=min(H-1,(cy+rad+65536)>>16)
        for y in range(miny,maxy+1):
            for x in range(minx,maxx+1):
                dx=((x<<16)+32768)-cx;dy=((y<<16)+32768)-cy;dist=math.isqrt(abs(dx)*abs(dx)+abs(dy)*abs(dy));cov=coverage(dist,rad)
                if cov:
                    idx=y*W+x;frame[idx]=blend(frame[idx],PRIMARY,cov,op)
    return b''.join(struct.pack('<HHHH',*px) for px in frame)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-strict-field-') as td0:
        td=pathlib.Path(td0);src=td/'p.c';exe=td/'p';raw=td/'o.bin';src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),'-I',str(root/'src/compositor'),'-I',str(root/'src/renderer'),'-I',str(root/'src/ir'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        r=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if r.returncode:raise SystemExit('strict field probe compile failed\n'+r.stdout+r.stderr)
        r=subprocess.run([str(exe),str(raw)],cwd=root,capture_output=True,text=True)
        if r.returncode:raise SystemExit(f'strict field probe failed rc={r.returncode}\n{r.stdout}\n{r.stderr}')
        got=raw.read_bytes()
    fb=W*H*8
    if len(got)!=4*fb:raise SystemExit(f'size mismatch {len(got)}')
    f=[got[i*fb:(i+1)*fb] for i in range(4)];e0=expected(False);e1=expected(True)
    if f[0]!=e0:
        j=next(i for i,(x,y) in enumerate(zip(f[0],e0)) if x!=y);raise SystemExit(f'strict baseline pixel mismatch byte={j} got={f[0][j]} expected={e0[j]}')
    if f[1]!=e0 or f[2]!=e0:raise SystemExit('strict field changed under sample/tick/ring/config-seed perturbation')
    if f[3]!=e1:raise SystemExit('strict music-motion reconstruction mismatch')
    if f[3]==f[0]:raise SystemExit('radial music change did not move/change strict particle raster')
    print('STRICT CAUSAL FIELD ORACLE: PASS')
    print('  96 particles / 96 radial sectors reconstructed pixel-exact')
    print('  sample, tick, ring phase and config seed perturbations: byte-identical under held music')
    print('  changing radial lane 40 changes particle geometry/opacity deterministically')
    print('  baseline sha256='+hashlib.sha256(e0).hexdigest())
    print('  changed  sha256='+hashlib.sha256(e1).hexdigest())
    return 0
if __name__=='__main__':raise SystemExit(main())
