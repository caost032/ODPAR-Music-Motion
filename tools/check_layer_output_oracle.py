#!/usr/bin/env python3
"""Independent pixel oracle for ODPAR 0.14 canonical Layer Outputs.

The engine emits BACKGROUND/CORE/FIELD/HUD separately as transparent-black
RGBA16LE linear-premultiplied buffers. Python reconstructs every pixel of a
small canonical scene from the public geometry contract and compares bytes and
SHA-256. Frame-plan scalars are inputs here; their independent truth is covered
by check_layered_oracle.py.
"""
from __future__ import annotations
import argparse, hashlib, math, pathlib, struct, subprocess, tempfile
Q=2147483647
LAYERS=(1,2,3,4)
PROBE=r'''#include "odm_compositor.h"
#include "odm_renderer.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t q(uint32_t n,uint32_t d){return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);}
static void w16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void hx(const uint8_t*p,size_t n){size_t i;for(i=0;i<n;++i)printf("%02x",p[i]);}
int main(int ac,char**av){
 odm_layered_config c;odm_composition_frame_state co;odm_director_frame_state di;odm_layered_frame_plan p;
 odm_render_surface_frame s;uint8_t sp[2*2*8];uint64_t fb=0,sb=0,sfb=0,ssb=0,rf=0,rs=0;void*scratch=0;void*sscratch=0;uint8_t*out=0;uint8_t*sout=0;odm_sha256_digest h;FILE*f;unsigned i,lid;
 if(ac!=2)return 2;
 memset(&c,0,sizeof(c));memset(&co,0,sizeof(co));memset(&di,0,sizeof(di));memset(&s,0,sizeof(s));
 c.schema_version=1;c.canvas.schema_version=1;c.canvas.aspect=ODM_CANVAS_ASPECT_SQUARE_1_1;c.canvas.width=32;c.canvas.height=32;c.canvas.fps=30;
 c.canvas.safe_left_q31=c.canvas.safe_top_q31=c.canvas.safe_right_q31=c.canvas.safe_bottom_q31=q(1,16);
 c.background.schema_version=1;c.background.style=ODM_BACKGROUND_SOLID;c.background.solid_color.a=65535;c.background.opacity_q31=INT32_MAX;
 c.core.schema_version=1;c.core.shape=ODM_CORE_SHAPE_CIRCLE;c.core.fit=ODM_CORE_FIT_STRETCH;c.core.center_x_q31=c.core.center_y_q31=q(1,2);c.core.width_q31=c.core.height_q31=q(1,2);c.core.border_q16=0;c.core.feather_q16=1u<<16;c.core.opacity_q31=INT32_MAX;
 c.field.schema_version=1;c.field.flags=ODM_FIELD_ORBIT_RING;c.field.radial_segments=48;c.field.particle_count=0;c.field.ring_gap_q16=2u<<16;c.field.bar_min_q16=1u<<16;c.field.bar_max_q16=2u<<16;c.field.bar_width_q16=1u<<16;c.field.particle_radius_q16=1u<<16;c.field.field_opacity_q31=INT32_MAX;c.field.secondary_color.r=65535;c.field.secondary_color.g=65535;c.field.secondary_color.b=65535;c.field.secondary_color.a=65535;
 c.hud.schema_version=1;c.hud.flags=ODM_HUD_PROGRESS_BAR;c.hud.margin_q16=1u<<16;c.hud.progress_height_q16=1u<<16;c.hud.progress_width_q31=q(3,4);c.hud.text_scale_q16=1u<<16;c.hud.line_gap_q16=1u<<16;c.hud.opacity_q31=INT32_MAX;c.hud.foreground_color.r=65535;c.hud.foreground_color.g=65535;c.hud.foreground_color.b=65535;c.hud.foreground_color.a=65535;c.hud.background_color.a=65535;
 co.schema_version=ODM_COMPOSITION_SCHEMA_VERSION;co.mode=ODM_COMPOSITION_MODE_FLOW;co.tick_index=50;co.center_sample=24000;co.radial_gain_q31=INT32_MAX;
 di.schema_version=ODM_DIRECTOR_SCHEMA_VERSION;di.tick_index=50;di.layout=ODM_DIRECTOR_LAYOUT_MONOLITH;
 for(i=0;i<4;i++){uint16_t v[4]={12000,6000,18000,50000};unsigned ch;for(ch=0;ch<4;ch++)w16(sp+i*8+ch*2,v[ch]);}
 s.width=2;s.height=2;s.pixel_format=ODM_RENDER_SURFACE_RGBA16LE_LINEAR_PREMUL;s.primaries=ODM_COLOR_PRIMARIES_BT709;s.transfer=ODM_COLOR_TRANSFER_LINEAR;s.alpha_mode=ODM_ALPHA_PREMULTIPLIED;s.pixel_bytes=sizeof(sp);s.pixels=sp;
 if(odm_layered_config_validate(&c)!=ODM_STATUS_OK)return 3;
 if(odm_layered_resolve_frame_plan(&c,&co,&di,24000,48000,&p)!=ODM_STATUS_OK)return 4;
 if(odm_layered_render_layer_requirements(&c,&fb,&sb)!=ODM_STATUS_OK)return 5;
 if(odm_layered_render_requirements(&c,ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,&sfb,&ssb)!=ODM_STATUS_OK)return 6;
 if(sfb!=fb)return 7;
 if(posix_memalign(&scratch,8,(size_t)sb)!=0)return 8;
 if(posix_memalign(&sscratch,8,(size_t)ssb)!=0)return 9;
 out=(uint8_t*)malloc((size_t)fb);sout=(uint8_t*)malloc((size_t)sfb);
 if(!out||!sout)return 10;
 f=fopen(av[1],"wb");if(!f)return 11;
 printf("M,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%u,%u,%d,%d\n",p.width,p.height,p.center_x_q16,p.center_y_q16,p.core_radius_q16,p.core_half_w_q16,p.core_half_h_q16,p.ring_inner_radius_q16,p.safe_left_q16,p.safe_top_q16,p.safe_right_q16,p.safe_bottom_q16,p.core_opacity_q31,p.field_opacity_q31,p.progress_q31,c.field.ring_gap_q16,c.field.bar_width_q16);
 for(lid=ODM_LAYERED_LAYER_BACKGROUND;lid<=ODM_LAYERED_LAYER_HUD;++lid){
   memset(out,0xA5,(size_t)fb);
   if(odm_layered_render_layer_rgba16(&c,&p,&s,lid,0,scratch,sb,out,fb,&rf,&rs,&h)!=ODM_STATUS_OK)return 12;
   if(fwrite(out,1,(size_t)fb,f)!=(size_t)fb)return 13;
   printf("H,%u,",lid);hx(h.bytes,32);putchar('\n');
 }
 if(odm_layered_render_stack_rgba16(&c,&p,&s,ODM_LAYERED_LAYER_MASK_ALL,0,sscratch,ssb,sout,sfb,&rf,&rs,&h)!=ODM_STATUS_OK)return 14;
 if(fwrite(sout,1,(size_t)sfb,f)!=(size_t)sfb)return 15;
 printf("A,");hx(h.bytes,32);putchar('\n');
 if(fclose(f)!=0)return 16;
 free(sout);
 free(out);
 free(sscratch);
 free(scratch);
 return 0;
}
'''

def mul_u16_q31(v,q): return min(65535,(v*q+Q//2)//Q)
def mul_u16_u16(a,b): return (a*b+32767)//65535

def blend(dst, src, cov, opacity):
    if not cov or not opacity:return dst
    q=(cov*opacity+Q//2)//Q
    a=mul_u16_q31(src[3],q)
    if not a:return dst
    sr=mul_u16_u16(src[0],a);sg=mul_u16_u16(src[1],a);sb=mul_u16_u16(src[2],a);inv=65535-a
    return tuple(min(65535,s+(d*inv+32767)//65535) for s,d in zip((sr,sg,sb,a),dst))

def blend_premul(dst,src,opacity=Q):
    s=tuple(mul_u16_q31(v,opacity) for v in src);inv=65535-s[3]
    return tuple(min(65535,sv+(dv*inv+32767)//65535) for sv,dv in zip(s,dst))

def signed_cov(sdf,feather):
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
    return signed_cov(math.isqrt(sumq)-radius,feather)

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

def c_div(n,d): return n//d if n>=0 else -((-n)//d)
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

def pack(px): return b''.join(struct.pack('<HHHH',*p) for p in px)

def expected_layers(vals):
    (w,h,cx,cy,radius,hw,hh,_ring_inner,safe_l,_safe_t,safe_r,safe_b,core_op,field_op,progress,ring_gap,bar_width)=vals
    transparent=(0,0,0,0); white=(65535,65535,65535,65535); black=(0,0,0,65535)
    bg=[black for _ in range(w*h)]
    core=[transparent for _ in range(w*h)]
    field=[transparent for _ in range(w*h)]
    hud=[transparent for _ in range(w*h)]
    full=[black for _ in range(w*h)]
    feather=1<<16; box_w=hw*2; box_h=hh*2; crop_w=2<<16; crop_h=2<<16
    for y in range(h):
        yq=(y<<16)+32768;dy=yq-cy;local_y=yq-cy+hh;sy=c_div(local_y*crop_h,box_h)-32768;iy,fy=floor_q16(sy)
        for x in range(w):
            xq=(x<<16)+32768;dx=xq-cx;ss=abs(dx)**2+abs(dy)**2;cov=circle_cov(ss,radius,feather)
            if cov:
                local_x=xq-cx+hw;sx=c_div(local_x*crop_w,box_w)-32768;ix,fx=floor_q16(sx);sample=uniform_bilinear(ix,iy,fx,fy)
                eff=(cov*core_op+Q//2)//Q;prem=tuple(mul_u16_q31(v,eff) for v in sample)
                core[y*w+x]=blend_premul(core[y*w+x],prem,Q)
                full[y*w+x]=blend_premul(full[y*w+x],prem,Q)
    target=radius+ring_gap;half=bar_width//2
    for y in range(h):
        dy=((y<<16)+32768)-cy
        for x in range(w):
            dx=((x<<16)+32768)-cx;dist=math.isqrt(abs(dx)**2+abs(dy)**2);cov=line_cov(abs(dist-target),half,65536)
            if cov:
                field[y*w+x]=blend(field[y*w+x],white,cov,field_op)
                full[y*w+x]=blend(full[y*w+x],white,cov,field_op)
    margin=1<<16;safe_width=safe_r-safe_l;bw=(safe_width*((Q*3+2)//4)+Q//2)//Q;l=safe_l+(safe_width-bw)//2;r=l+bw;b=safe_b-margin;t=b-(1<<16);fill=l+(bw*progress+Q//2)//Q
    for L,R,col in ((l,r,black),(l,fill,white)):
        for y in range(h):
            for x in range(w):
                cov=rect_cov(L,t,R,b,x,y)
                if cov:
                    hud[y*w+x]=blend(hud[y*w+x],col,cov,Q)
                    full[y*w+x]=blend(full[y*w+x],col,cov,Q)
    return [pack(x) for x in (bg,core,field,hud,full)]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-layer-output-oracle-') as td:
        td=pathlib.Path(td);src=td/'probe.c';exe=td/'probe';blob=td/'layers.bin';src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        r=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if r.returncode:raise SystemExit('layer-output probe compile failed\n'+r.stdout+r.stderr)
        r=subprocess.run([str(exe),str(blob)],cwd=root,capture_output=True,text=True)
        if r.returncode:raise SystemExit(f'layer-output probe failed rc={r.returncode}\n{r.stdout}\n{r.stderr}')
        data=blob.read_bytes(); lines=r.stdout.splitlines()
    meta=None; hashes={}; stack_hash=None
    for ln in lines:
        p=ln.split(',')
        if p[0]=='M':meta=list(map(int,p[1:]))
        elif p[0]=='H':hashes[int(p[1])]=p[2]
        elif p[0]=='A':stack_hash=p[1]
    if meta is None or set(hashes)!=set(LAYERS) or stack_hash is None:raise SystemExit('layer-output oracle missing metadata/hash rows')
    w,h=meta[0],meta[1];fb=w*h*8
    if len(data)!=fb*5:raise SystemExit(f'layer-output size mismatch got={len(data)} exp={fb*5}')
    expected=expected_layers(meta)
    names=('BACKGROUND','CORE','FIELD','HUD')
    for i,lid in enumerate(LAYERS):
        got=data[i*fb:(i+1)*fb]; exp=expected[i]
        if got!=exp:
            j=next(k for k,(u,v) in enumerate(zip(got,exp)) if u!=v)
            pix=j//8;ch=(j%8)//2
            raise SystemExit(f'{names[i]} mismatch byte={j} pixel=({pix%w},{pix//w}) channel={ch} got={got[j:j+8].hex()} exp={exp[j:j+8].hex()}')
        gh=hashlib.sha256(got).hexdigest()
        if gh!=hashes[lid]:raise SystemExit(f'{names[i]} SHA mismatch C={hashes[lid]} Python={gh}')
    got_stack=data[4*fb:5*fb]; exp_stack=expected[4]
    if got_stack!=exp_stack:
        j=next(k for k,(u,v) in enumerate(zip(got_stack,exp_stack)) if u!=v);pix=j//8;ch=(j%8)//2
        raise SystemExit(f'CANONICAL STACK mismatch byte={j} pixel=({pix%w},{pix//w}) channel={ch} got={got_stack[j:j+8].hex()} exp={exp_stack[j:j+8].hex()}')
    stack_py=hashlib.sha256(exp_stack).hexdigest()
    if stack_py!=stack_hash:raise SystemExit(f'CANONICAL STACK SHA mismatch C={stack_hash} Python={stack_py}')
    print('LAYER OUTPUT ORACLE: PASS')
    print(f'  canvas: {w}x{h}; layers=4; pixels/layer={w*h}; channels checked={w*h*4*4}')
    for i,lid in enumerate(LAYERS):print(f'  {names[i]} sha256={hashlib.sha256(expected[i]).hexdigest()}')
    print('  semantics: each public layer independently reconstructed over transparent black')
    print(f'  CANONICAL STACK sha256={stack_py} (direct authority-order reconstruction)')
    print('  nonclaim: flattened integer-alpha layer recomposition is not assumed associative')
    return 0
if __name__=='__main__':raise SystemExit(main())
