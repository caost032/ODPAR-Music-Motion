#!/usr/bin/env python3
import argparse, hashlib, pathlib, struct, subprocess, tempfile
Q=2147483647
PROBE=r'''
#include "odm_compositor.h"
#include "compositor_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void emit(const char *tag, int sym) {
    odm_layered_config c; odm_layered_frame_plan p; odm_layered_pixel16 f[48u*32u];
    uint32_t i;
    memset(&c,0,sizeof(c)); memset(&p,0,sizeof(p)); memset(f,0,sizeof(f));
    c.background.style=ODM_BACKGROUND_PERSPECTIVE_GRID;
    c.background.solid_color.a=UINT16_MAX;
    c.background.grid_color.r=UINT16_MAX;c.background.grid_color.g=UINT16_MAX;
    c.background.grid_color.b=UINT16_MAX;c.background.grid_color.a=UINT16_MAX;
    c.background.opacity_q31=(uint32_t)INT32_MAX;
    p.width=48u;p.height=32u;p.grid_spacing_q16=9<<16;p.grid_line_q16=1<<16;
    p.grid_feather_q16=1<<16;p.grid_depth_q16=2<<16;
    if(!sym){p.grid_offset_x_q16=(2<<16)+12345;p.grid_offset_y_q16=(3<<16)+23456;p.grid_warp_q16=1<<16;}
    odm_layered_background_render_rows(&c,&p,0u,p.height,f);
    printf("%s,",tag);
    for(i=0u;i<48u*32u;++i){
        uint16_t v[4]={f[i].r,f[i].g,f[i].b,f[i].a}; uint32_t j;
        for(j=0;j<4;j++){unsigned x=v[j];printf("%02x%02x",x&255u,(x>>8)&255u);}
    }
    putchar('\n');
}
int main(void){emit("A",0);emit("B",1);return 0;}
'''
def tdiv(a,b):
    assert b>0
    return a//b if a>=0 else -((-a)//b)
def div_near(a,b):
    assert b>0
    return (a+b//2)//b if a>=0 else -(((-a)+b//2)//b)
def fmod(a,m):
    r=a%m
    return r if r>=0 else r+m
def tri(coord,period):
    if period<=0:return 0
    r=fmod(coord,period); half=period//2
    if half<=0:return 0
    if r<=half:q=(r*131072+half//2)//half-65536
    else:q=((period-r)*131072+half//2)//half-65536
    return max(-2147483648,min(2147483647,q))
def near_grid(coord,spacing):
    r=fmod(coord,spacing)
    return min(r,spacing-r)
def coverage(dist,half,feather):
    if dist<0:dist=-dist
    if half<0:half=0
    if feather<=0:return Q if dist<=half else 0
    if dist<=half:return Q
    d=dist-half
    if d>=feather:return 0
    return ((feather-d)*Q+feather//2)//feather
def expected(sym):
    width,height=48,32; spacing=9<<16; line=1<<16; feather=1<<16; depth=2<<16
    offx=0 if sym else (2<<16)+12345; offy=0 if sym else (3<<16)+23456; warp=0 if sym else 1<<16
    half_h=height<<15; cy=half_h; out=bytearray()
    for y in range(height):
        yq=(y<<16)+32768; dy=yq-cy; ady=abs(dy); active=True
        if ady<65536 or half_h<=0: active=False
        if active:
            focal=half_h+depth*8
            scale=(focal*65536+ady//2)//ady
            if scale<=0:active=False
        if active:
            proj=(spacing*65536+scale//2)//scale
            if proj<131072:active=False
        if active:
            period4=spacing*4; rw=tri(yq+offy,period4); rshift=tdiv(rw*warp,65536)
            # C uses Q17 perspective-world coordinates so half-pixel centers
            # remain exact even when scale is odd.
            x0=(1-width)*scale + rshift*2 + offx*2
            xstep=scale*2
            z=div_near((half_h-ady)*scale,32768)+offy*2+div_near(rw*warp,32768)
            lh=div_near((line//2)*scale,32768); fw=div_near(feather*scale,32768)
            spacing17=spacing*2
        for x in range(width):
            q=0
            if active:
                wx=x0+x*xstep
                q=coverage(min(near_grid(wx,spacing17),near_grid(z,spacing17)),lh,fw)
            a=(65535*q+Q//2)//Q if q else 0
            out += struct.pack('<HHHH',a,a,a,65535)
    return bytes(out)
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='cc');ap.add_argument('--library',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve(); lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-perspective-oracle-') as td:
        td=pathlib.Path(td); src=td/'p.c'; exe=td/'p';src.write_text(PROBE+'\n')
        inc=['include','src/compositor','src/renderer','src/ir','src/music_map','src/visual']
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror']+sum((['-I',str(root/x)] for x in inc),[])+[str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        r=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if r.returncode: raise SystemExit('perspective probe compile failed\n'+r.stdout+r.stderr)
        r=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
        if r.returncode: raise SystemExit('perspective probe failed\n'+r.stdout+r.stderr)
    got={ln[0]:bytes.fromhex(ln[2:]) for ln in r.stdout.splitlines() if len(ln)>2 and ln[1]==','}
    for tag,sym in [('A',False),('B',True)]:
        exp=expected(sym)
        if got.get(tag)!=exp:
            g=got.get(tag,b''); n=min(len(g),len(exp)); i=next((i for i in range(n) if g[i]!=exp[i]),n)
            raise SystemExit(f'perspective {tag} mismatch byte={i} got_len={len(g)} exp_len={len(exp)}')
    b=got['B']; w=48;h=32;px=8
    for y in range(h):
        for x in range(w):
            i=(y*w+x)*px; ix=(y*w+(w-1-x))*px; iy=((h-1-y)*w+x)*px
            if b[i:i+px]!=b[ix:ix+px] or b[i:i+px]!=b[iy:iy+px]:
                raise SystemExit(f'perspective symmetry mismatch x={x} y={y}')
    print('PERSPECTIVE GRID ORACLE: PASS')
    print(f'  reactive raster: {w*h} pixels / {len(got["A"])} bytes sha256={hashlib.sha256(got["A"]).hexdigest()}')
    print(f'  symmetric raster: {w*h} pixels / {len(b)} bytes sha256={hashlib.sha256(b).hexdigest()}')
    print('  independent fixed-point projection + horizon suppression + RGBA16 reconstruction exact')
if __name__=='__main__':main()
