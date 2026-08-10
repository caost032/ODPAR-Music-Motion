#!/usr/bin/env python3
"""Independent ODPAR 0.13 HUD geometry oracle.

Reconstructs the capsule progress raster from literal policy inputs. The C
implementation is observed only through its final RGBA16 bytes.
"""
from __future__ import annotations
import argparse, hashlib, math, pathlib, struct, subprocess, tempfile
Q=2147483647
PROBE=r'''
#include "odm_compositor.h"
#include "odm_renderer.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t q(uint32_t n,uint32_t d){return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);}
int main(int ac,char **av){
 odm_layered_config c;odm_composition_frame_state co;odm_director_frame_state di;
 odm_layered_frame_plan p;odm_render_surface_frame s;uint8_t src[8]={0};
 uint64_t fb=0,sb=0,rf=0,rs=0;void *scratch=0;uint8_t *out=0;odm_sha256_digest h;FILE *f;
 if(ac!=2)return 2;
 memset(&c,0,sizeof(c));
 memset(&co,0,sizeof(co));
 memset(&di,0,sizeof(di));
 memset(&s,0,sizeof(s));
 c.schema_version=ODM_LAYERED_SCHEMA_VERSION;c.canvas.schema_version=ODM_LAYERED_SCHEMA_VERSION;
 c.canvas.aspect=ODM_CANVAS_ASPECT_SQUARE_1_1;c.canvas.width=32;c.canvas.height=32;c.canvas.fps=30;
 c.canvas.safe_left_q31=c.canvas.safe_top_q31=c.canvas.safe_right_q31=c.canvas.safe_bottom_q31=q(1,16);
 c.background.schema_version=ODM_LAYERED_SCHEMA_VERSION;c.background.style=ODM_BACKGROUND_NONE;
 c.core.schema_version=ODM_LAYERED_SCHEMA_VERSION;c.core.shape=ODM_CORE_SHAPE_CIRCLE;c.core.fit=ODM_CORE_FIT_STRETCH;
 c.core.center_x_q31=c.core.center_y_q31=q(1,2);c.core.width_q31=c.core.height_q31=q(1,2);c.core.feather_q16=1u<<16;c.core.opacity_q31=0;
 c.field.schema_version=ODM_LAYERED_SCHEMA_VERSION;c.field.radial_segments=ODM_COMPOSITION_RADIAL_SEGMENTS;c.field.flags=0;
 c.hud.schema_version=ODM_LAYERED_SCHEMA_VERSION;c.hud.flags=ODM_HUD_PROGRESS_BAR;c.hud.margin_q16=2u<<16;
 c.hud.progress_height_q16=3u<<16;c.hud.progress_width_q31=q(3,4);c.hud.text_scale_q16=1u<<16;c.hud.line_gap_q16=1u<<16;
 c.hud.opacity_q31=INT32_MAX;c.hud.foreground_color.r=c.hud.foreground_color.g=c.hud.foreground_color.b=c.hud.foreground_color.a=65535;
 /* La barra de progreso tiene COLOR PROPIO: `foreground_color` gobierna el
  * codigo de tiempo, no la barra. Fijar solo el primero y esperar una barra
  * blanca era la suposicion que este oraculo hacia y que ya no se sostiene. */
 c.hud.progress_color.r=c.hud.progress_color.g=c.hud.progress_color.b=c.hud.progress_color.a=65535;
 c.hud.progress_track_color.a=65535;
 c.hud.background_color.a=65535;c.hud.progress_style=ODM_HUD_PROGRESS_CAPSULE;c.hud.metadata_anchor=ODM_HUD_ANCHOR_BOTTOM_CENTER;c.hud.time_mode=ODM_HUD_TIME_ELAPSED_TOTAL;
 co.schema_version=ODM_COMPOSITION_SCHEMA_VERSION;co.tick_index=50;di.schema_version=ODM_DIRECTOR_SCHEMA_VERSION;di.tick_index=50;di.layout=ODM_DIRECTOR_LAYOUT_MONOLITH;
 s.width=1;s.height=1;s.pixel_format=ODM_RENDER_SURFACE_RGBA16LE_LINEAR_PREMUL;s.primaries=ODM_COLOR_PRIMARIES_BT709;s.transfer=ODM_COLOR_TRANSFER_LINEAR;s.alpha_mode=ODM_ALPHA_PREMULTIPLIED;s.pixel_bytes=sizeof(src);s.pixels=src;
 if(odm_layered_resolve_frame_plan(&c,&co,&di,24000,48000,&p)!=ODM_STATUS_OK)return 3;
 if(odm_layered_render_requirements(&c,ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,&fb,&sb)!=ODM_STATUS_OK)return 4;
 if(posix_memalign(&scratch,8,(size_t)sb)!=0)return 5;
 out=(uint8_t*)malloc((size_t)fb);
 if(!out)return 6;
 f=fopen(av[1],"wb");
 if(!f)return 8;
 /* Tres renders sobre la MISMA geometria. El primero es el vector de
  * referencia; los otros dos son el control negativo de la separacion de
  * categorias: mover el color de fondo del HUD no puede tocar la barra, y
  * mover el color de la pista tiene que tocarla. Sin los dos, este oraculo
  * comprobaria la geometria y se creeria a ciegas de quien pinta que. */
 {
  uint32_t v;
  for(v=0u;v<3u;++v){
   odm_layered_config cv=c;
   if(v==1u){cv.hud.background_color.r=cv.hud.background_color.g=cv.hud.background_color.b=65535;}
   if(v==2u){cv.hud.progress_track_color.r=cv.hud.progress_track_color.g=cv.hud.progress_track_color.b=65535;}
   if(odm_layered_resolve_frame_plan(&cv,&co,&di,24000,48000,&p)!=ODM_STATUS_OK)return 3;
   if(odm_layered_render_frame(&cv,&p,&s,0,ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,scratch,sb,out,fb,&rf,&rs,&h)!=ODM_STATUS_OK)return 7;
   if(fwrite(out,1,(size_t)fb,f)!=(size_t)fb)return 9;
  }
 }
 if(fclose(f)!=0)return 10;
 free(out);
 free(scratch);
 return 0;
}
'''

def qr(n,d): return (Q*n+d//2)//d

def ratio_px(dim,q): return min(2147483647,(dim*65536*q+Q//2)//Q)

def cov_sdf(sdf:int)->int:
    half=32768; feather=65536
    if sdf<=-half:return Q
    if sdf>=half:return 0
    return ((half-sdf)*Q+feather//2)//feather

def rr_sdf(px,py,l,t,r,b):
    hw=(r-l)//2;hh=(b-t)//2;cx=l+hw;cy=t+hh;rad=min(hw,hh)
    qx=abs(px-cx)-(hw-rad);qy=abs(py-cy)-(hh-rad)
    ox=max(qx,0);oy=max(qy,0)
    outside=math.isqrt(ox*ox+oy*oy);inside=min(max(qx,qy),0)
    return outside+inside-rad

def mul_u16_q31(v,q): return min(65535,(v*q+Q//2)//Q)
def mul_u16_u16(a,b): return (a*b+32767)//65535

def blend(dst,src,cov):
    if not cov:return dst
    a=mul_u16_q31(src[3],cov)
    if not a:return dst
    sr=mul_u16_u16(src[0],a);sg=mul_u16_u16(src[1],a);sb=mul_u16_u16(src[2],a);inv=65535-a
    return tuple(min(65535,s+(d*inv+32767)//65535) for s,d in zip((sr,sg,sb,a),dst))

def draw_rr(px,w,h,l,t,r,b,color):
    if l>=r or t>=b:return
    for y in range(h):
        py=(y<<16)+32768
        for x in range(w):
            p=(x<<16)+32768
            c=cov_sdf(rr_sdf(p,py,l,t,r,b))
            if c:px[y*w+x]=blend(px[y*w+x],color,c)

def expected():
    w=h=32;safe=ratio_px(32,qr(1,16));safe_l=safe;safe_r=(32<<16)-safe;safe_b=(32<<16)-safe
    margin=2<<16;ph=3<<16;full=safe_r-safe_l;bw=(full*qr(3,4)+Q//2)//Q
    l=safe_l+(full-bw)//2;r=l+bw;b=safe_b-margin;t=b-ph;progress=(24000*Q+24000)//48000;fill=l+(bw*progress+Q//2)//Q
    px=[(0,0,0,0) for _ in range(w*h)]
    draw_rr(px,w,h,l,t,r,b,(0,0,0,65535));draw_rr(px,w,h,l,t,fill,b,(65535,65535,65535,65535))
    return b''.join(struct.pack('<HHHH',*v) for v in px)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-hud-oracle-') as td:
        td=pathlib.Path(td);src=td/'p.c';exe=td/'p';out=td/'f.bin';src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        r=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if r.returncode:raise SystemExit('HUD oracle probe compile failed\n'+r.stdout+r.stderr)
        r=subprocess.run([str(exe),str(out)],cwd=root,capture_output=True,text=True)
        if r.returncode:raise SystemExit(f'HUD oracle probe failed rc={r.returncode}\n{r.stdout}\n{r.stderr}')
        raw=out.read_bytes()
    exp=expected()
    n=len(exp)
    if len(raw)!=3*n:
        raise SystemExit(f'HUD probe entrego {len(raw)} bytes, se esperaban {3*n}')
    got, sin_fondo, con_pista = raw[:n], raw[n:2*n], raw[2*n:]
    if got!=exp:
        i=next((i for i in range(n) if got[i]!=exp[i]),n)
        raise SystemExit(f'HUD capsule mismatch byte={i} got_len={len(got)} exp_len={len(exp)}')
    if sin_fondo!=got:
        raise SystemExit('el color de fondo del HUD sigue pintando la barra de progreso')
    if con_pista==got:
        raise SystemExit('el color de la pista no pinta la barra de progreso')
    print('HUD GEOMETRY ORACLE: PASS')
    print(f'  raster: 32x32 / {len(got)} bytes / sha256={hashlib.sha256(got).hexdigest()}')
    print('  capsule track/fill: independent integer-SDF + 1px-AA reconstruction exact')
if __name__=='__main__':raise SystemExit(main())
