#!/usr/bin/env python3
"""Independent pixel oracle for ODPAR 0.13 layered raster.

Scope is deliberately orthogonal to check_layered_oracle.py: that oracle proves
frame-plan fixed-point resolution; this one treats the observed plan scalars as
inputs and independently reconstructs the canonical RGBA16 raster for a small
scene containing a circular Core, feathered orbit ring and progress HUD.
"""
from __future__ import annotations
import argparse, pathlib, struct, subprocess, tempfile
Q=2147483647
PROBE=r'''#include "odm_compositor.h"
#include "odm_renderer.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t q(uint32_t n,uint32_t d){return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);}
static void w16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
int main(int ac,char**av){
 odm_layered_config c;odm_composition_frame_state co;odm_director_frame_state di;odm_layered_frame_plan p;
 odm_render_surface_frame s;uint8_t sp[2*2*8];uint64_t fb=0,sb=0,rf=0,rs=0;void*scratch=0;uint8_t*out=0;odm_sha256_digest h;FILE*f;unsigned i;
 if(ac!=3)return 2;
 memset(&c,0,sizeof(c));memset(&co,0,sizeof(co));memset(&di,0,sizeof(di));memset(&s,0,sizeof(s));
 c.schema_version=1;c.canvas.schema_version=1;c.canvas.aspect=ODM_CANVAS_ASPECT_SQUARE_1_1;c.canvas.width=32;c.canvas.height=32;c.canvas.fps=30;
 c.canvas.safe_left_q31=c.canvas.safe_top_q31=c.canvas.safe_right_q31=c.canvas.safe_bottom_q31=q(1,16);
 c.background.schema_version=1;c.background.style=ODM_BACKGROUND_SOLID;c.background.solid_color.a=65535;c.background.opacity_q31=INT32_MAX;
 c.core.schema_version=1;c.core.shape=ODM_CORE_SHAPE_CIRCLE;c.core.fit=ODM_CORE_FIT_STRETCH;c.core.center_x_q31=c.core.center_y_q31=q(1,2);c.core.width_q31=c.core.height_q31=q(1,2);c.core.corner_radius_q31=0;c.core.border_q16=0;c.core.feather_q16=1u<<16;c.core.scale_reactivity_q31=0;c.core.opacity_q31=INT32_MAX;
 c.field.schema_version=1;c.field.flags=ODM_FIELD_ORBIT_RING;c.field.radial_segments=48;c.field.particle_count=0;c.field.ring_gap_q16=2u<<16;c.field.bar_min_q16=1u<<16;c.field.bar_max_q16=2u<<16;c.field.bar_width_q16=1u<<16;c.field.particle_radius_q16=1u<<16;c.field.field_opacity_q31=INT32_MAX;c.field.secondary_color.r=65535;c.field.secondary_color.g=65535;c.field.secondary_color.b=65535;c.field.secondary_color.a=65535;
 c.hud.schema_version=1;c.hud.flags=ODM_HUD_PROGRESS_BAR;c.hud.margin_q16=1u<<16;c.hud.progress_height_q16=1u<<16;c.hud.progress_width_q31=q(3,4);c.hud.text_scale_q16=1u<<16;c.hud.line_gap_q16=1u<<16;c.hud.opacity_q31=INT32_MAX;c.hud.foreground_color.r=65535;c.hud.foreground_color.g=65535;c.hud.foreground_color.b=65535;c.hud.foreground_color.a=65535;c.hud.background_color.a=65535;
 co.schema_version=ODM_COMPOSITION_SCHEMA_VERSION;co.mode=ODM_COMPOSITION_MODE_FLOW;co.tick_index=50;co.center_sample=24000;co.phase=0;co.ring_phase=0;co.core_breath_q31=0;co.grid_q31=0;co.particles_q31=0;co.radial_gain_q31=INT32_MAX;
 di.schema_version=ODM_DIRECTOR_SCHEMA_VERSION;di.tick_index=50;di.layout=ODM_DIRECTOR_LAYOUT_MONOLITH;
 for(i=0;i<4;i++){uint16_t v[4]={12000,6000,18000,50000};unsigned ch;for(ch=0;ch<4;ch++)w16(sp+i*8+ch*2,v[ch]);}
 s.width=2;s.height=2;s.pixel_format=ODM_RENDER_SURFACE_RGBA16LE_LINEAR_PREMUL;s.primaries=ODM_COLOR_PRIMARIES_BT709;s.transfer=ODM_COLOR_TRANSFER_LINEAR;s.alpha_mode=ODM_ALPHA_PREMULTIPLIED;s.pixel_bytes=sizeof(sp);s.pixels=sp;
 if(odm_layered_config_validate(&c)!=ODM_STATUS_OK)return 3;
 if(odm_layered_resolve_frame_plan(&c,&co,&di,24000,48000,&p)!=ODM_STATUS_OK)return 4;
 if(odm_layered_render_requirements(&c,ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,&fb,&sb)!=ODM_STATUS_OK)return 5;
 if(posix_memalign(&scratch,8,(size_t)sb)!=0)return 6;
 out=(uint8_t*)malloc((size_t)fb);if(!out)return 7;
 if(odm_layered_render_frame(&c,&p,&s,0,ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,scratch,sb,out,fb,&rf,&rs,&h)!=ODM_STATUS_OK)return 8;
 f=fopen(av[1],"wb");if(!f)return 9;if(fwrite(out,1,(size_t)fb,f)!=(size_t)fb)return 10;if(fclose(f)!=0)return 11;
 f=fopen(av[2],"w");if(!f)return 12;fprintf(f,"%u %u %d %d %d %d %d %d %d %d %d %d %u %u %u %d %d\n",p.width,p.height,p.center_x_q16,p.center_y_q16,p.core_radius_q16,p.core_half_w_q16,p.core_half_h_q16,p.ring_inner_radius_q16,p.safe_left_q16,p.safe_top_q16,p.safe_right_q16,p.safe_bottom_q16,p.core_opacity_q31,p.field_opacity_q31,p.progress_q31,c.field.ring_gap_q16,c.field.bar_width_q16);fclose(f);free(out);free(scratch);return 0;}
'''

def isqrt(x:int)->int:
    return int(x**0.5) if x < (1<<53) else __import__('math').isqrt(x)

def mul_u16_q31(v,q): return min(65535,(v*q+Q//2)//Q)
def mul_u16_u16(a,b): return (a*b+32767)//65535

def blend(dst, src, cov, opacity):
    if not cov or not opacity:return dst
    q=(cov*opacity+Q//2)//Q
    a=mul_u16_q31(src[3],q)
    if not a:return dst
    sr=mul_u16_u16(src[0],a);sg=mul_u16_u16(src[1],a);sb=mul_u16_u16(src[2],a);inv=65535-a
    return tuple(min(65535, s+(d*inv+32767)//65535) for s,d in zip((sr,sg,sb,a),dst))

def blend_premul(dst,src,opacity=Q):
    s=tuple(mul_u16_q31(v,opacity) for v in src);inv=65535-s[3]
    return tuple(min(65535, sv+(dv*inv+32767)//65535) for sv,dv in zip(s,dst))

def signed_cov(sdf, feather):
    if feather<=0:return Q if sdf<=0 else 0
    half=feather//2
    if sdf<=-half:return Q
    if sdf>=half:return 0
    return max(0,min(Q,((half-sdf)*Q+feather//2)//feather))

def circle_cov(sumq,radius,feather):
    if feather<=0:return Q if sumq<(radius+1)*(radius+1) else 0
    half=feather//2;inner=radius-half;outer=radius+half
    if inner>=0 and sumq<(inner+1)*(inner+1):return Q
    if outer<=0 or sumq>=outer*outer:return 0
    return signed_cov(__import__('math').isqrt(sumq)-radius,feather)

def line_cov(distance,half,feather):
    distance=abs(distance);half=max(0,half)
    if feather<=0:return Q if distance<=half else 0
    if distance<=half:return Q
    d=distance-half
    if d>=feather:return 0
    return ((feather-d)*Q+feather//2)//feather

def rect_cov(l,t,r,b,x,y):
    pl=x<<16;pt=y<<16;pr=pl+65536;pb=pt+65536
    ox=min(r,pr)-max(l,pl);oy=min(b,pb)-max(t,pt)
    if ox<=0 or oy<=0:return 0
    return ((ox*oy*Q)+(1<<31))>>32

def c_div(n,d):
    return n//d if n>=0 else -((-n)//d)

def floor_q16(q):
    i=c_div(q,65536);r=q-i*65536
    if r<0:i-=1;r+=65536
    return i,r

def lerp16(a,b,t): return (a*(65536-t)+b*t+32768)>>16

def uniform_bilinear(ix,iy,fx,fy):
    src=(12000,6000,18000,50000)
    def tex(x,y): return src if 0<=x<2 and 0<=y<2 else (0,0,0,0)
    p00,p10,p01,p11=tex(ix,iy),tex(ix+1,iy),tex(ix,iy+1),tex(ix+1,iy+1)
    out=[]
    for ch in range(4):
        top=lerp16(p00[ch],p10[ch],fx);bot=lerp16(p01[ch],p11[ch],fx);out.append(lerp16(top,bot,fy))
    return tuple(out)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args();root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-layered-raster-') as td:
        td=pathlib.Path(td);c=td/'p.c';exe=td/'p';frame=td/'f.bin';meta=td/'m.txt';c.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(c),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        r=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if r.returncode:raise SystemExit('raster probe compile failed\n'+r.stdout+r.stderr)
        r=subprocess.run([str(exe),str(frame),str(meta)],cwd=root,capture_output=True,text=True)
        if r.returncode:raise SystemExit(f'raster probe failed rc={r.returncode}\n{r.stdout}\n{r.stderr}')
        got=frame.read_bytes(); vals=list(map(int,meta.read_text().split()))
    (w,h,cx,cy,radius,hw,hh,ring_inner,safe_l,safe_t,safe_r,safe_b,core_op,field_op,progress,ring_gap,bar_width)=vals
    px=[(0,0,0,65535) for _ in range(w*h)]
    # Core: uniform premultiplied sample, circular mask with 1px canonical feather.
    feather=1<<16; box_w=hw*2; box_h=hh*2; crop_w=2<<16; crop_h=2<<16
    for y in range(h):
        yq=(y<<16)+32768;dy=yq-cy
        local_y=yq-cy+hh; sy=c_div(local_y*crop_h,box_h)-32768; iy,fy=floor_q16(sy)
        for x in range(w):
            xq=(x<<16)+32768;dx=xq-cx;s=abs(dx)**2+abs(dy)**2
            cov=circle_cov(s,radius,feather)
            if cov:
                local_x=xq-cx+hw; sx=c_div(local_x*crop_w,box_w)-32768; ix,fx=floor_q16(sx)
                sample=uniform_bilinear(ix,iy,fx,fy)
                eff=(cov*core_op+Q//2)//Q
                core=tuple(mul_u16_q31(v,eff) for v in sample)
                px[y*w+x]=blend_premul(px[y*w+x],core,Q)
    # Orbit ring: final canonical SDF/coverage, independent of candidate-span optimization.
    target=radius+ring_gap;half=bar_width//2;white=(65535,65535,65535,65535)
    for y in range(h):
        dy=((y<<16)+32768)-cy
        for x in range(w):
            dx=((x<<16)+32768)-cx;dist=__import__('math').isqrt(abs(dx)**2+abs(dy)**2)
            cov=line_cov(abs(dist-target),half,65536)
            if cov:px[y*w+x]=blend(px[y*w+x],white,cov,field_op)
    # HUD progress bar, same exact subpixel rectangle coverage; background is opaque black.
    margin=1<<16;full=safe_r-safe_l;bw=(full*((Q*3+2)//4)+Q//2)//Q;l=safe_l+(full-bw)//2;r=l+bw;b=safe_b-margin;t=b-(1<<16);fill=l+(bw*progress+Q//2)//Q
    black=(0,0,0,65535)
    for L,R,col in ((l,r,black),(l,fill,white)):
        for y in range(h):
            for x in range(w):
                cov=rect_cov(L,t,R,b,x,y)
                if cov:px[y*w+x]=blend(px[y*w+x],col,cov,Q)
    exp=b''.join(struct.pack('<HHHH',*p) for p in px)
    if got!=exp:
        for i,(a0,b0) in enumerate(zip(got,exp)):
            if a0!=b0:
                pix=i//8;ch=(i%8)//2; x=pix%w; y=pix//w
                dx=((x<<16)+32768)-cx; dy=((y<<16)+32768)-cy; ss=abs(dx)**2+abs(dy)**2
                print('DEBUG plan',vals)
                print('DEBUG pixel',x,y,'dx',dx,'dy',dy,'isqrt',__import__('math').isqrt(ss),'radius',radius,'core_cov',circle_cov(ss,radius,1<<16),'ring_cov',line_cov(abs(__import__('math').isqrt(ss)-(radius+ring_gap)),bar_width//2,65536))
                raise SystemExit(f'layered raster mismatch byte={i} pixel=({x},{y}) channel={ch} got={got[i:i+8].hex()} exp={exp[i:i+8].hex()}')
        raise SystemExit(f'layered raster size mismatch got={len(got)} exp={len(exp)}')
    print('LAYERED RASTER ORACLE: PASS')
    print(f'  canvas: {w}x{h}, pixels={w*h}, channels={w*h*4}')
    print('  layers: solid background + circular feathered core + exact orbit ring + progress HUD')
    print('  note: frame-plan scalars are independently verified by check_layered_oracle.py')
    return 0
if __name__=='__main__':raise SystemExit(main())
