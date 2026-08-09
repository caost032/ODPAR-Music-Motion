#!/usr/bin/env python3
"""Independent pixel oracle for multiscale causal radial raster v1.

The engine is observed only through odm_layered_field_render(). Expected pixels
are rebuilt in Python from fixed Q1.31 body/release/attack/final tuples plus the
published Q24.8 segment raster model. No production C source is parsed and no C
output is used to derive expected values.
"""
from __future__ import annotations
import argparse, hashlib, pathlib, struct, subprocess, sys, tempfile
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import check_radial_hires_geometry_oracle as g
import odm_radial_spec as spec

Q=2147483647
C035=751619277
W=H=64
FIELD_OP=(Q*3+2)//4
PRIMARY=(62000,18000,9000,65535)
SECONDARY=(10000,22000,61000,65535)
# sector -> (body, release excess, final, attack)
TUPLES={
    0:(400_000_000,500_000_000,1_600_000_000,1_000_000_000),
    24:(800_000_000,200_000_000,1_200_000_000,400_000_000),
    48:(100_000_000,1_000_000_000,1_800_000_000,1_500_000_000),
}

PROBE=r'''
#include "odm_compositor.h"
#include "compositor_internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t q(uint32_t n,uint32_t d){return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);}
static void tuple(odm_layered_frame_plan *p,uint32_t i,uint32_t body,uint32_t release,uint32_t final,uint32_t attack){
 p->radial_body_q31[i]=body;p->radial_release_q31[i]=release;p->radial_q31[i]=final;p->radial_attack_q31[i]=attack;
}
int main(int ac,char**av){
 odm_layered_config c;odm_layered_frame_plan p;odm_layered_pixel16 *f;FILE*out;
 if(ac!=2)return 2;
 memset(&c,0,sizeof(c));memset(&p,0,sizeof(p));
 c.schema_version=ODM_LAYERED_SCHEMA_VERSION;
 c.canvas.schema_version=1;c.canvas.aspect=ODM_CANVAS_ASPECT_SQUARE_1_1;c.canvas.width=64;c.canvas.height=64;c.canvas.fps=30;
 c.background.schema_version=1;c.background.style=ODM_BACKGROUND_NONE;c.background.opacity_q31=INT32_MAX;
 c.core.schema_version=1;c.core.shape=ODM_CORE_SHAPE_CIRCLE;c.core.fit=ODM_CORE_FIT_STRETCH;c.core.center_x_q31=q(1,2);c.core.center_y_q31=q(1,2);c.core.width_q31=q(1,4);c.core.height_q31=q(1,4);c.core.feather_q16=1u<<16;c.core.opacity_q31=INT32_MAX;
 c.field.schema_version=1;c.field.flags=ODM_FIELD_RADIAL_BARS;c.field.radial_segments=96;c.field.ring_gap_q16=1u<<16;c.field.bar_min_q16=0;c.field.bar_max_q16=10u<<16;c.field.bar_width_q16=1u<<16;c.field.particle_radius_q16=1u<<16;c.field.field_opacity_q31=q(3,4);c.field.primary_color.r=62000;c.field.primary_color.g=18000;c.field.primary_color.b=9000;c.field.primary_color.a=65535;c.field.secondary_color.r=10000;c.field.secondary_color.g=22000;c.field.secondary_color.b=61000;c.field.secondary_color.a=65535;
 c.hud.schema_version=1;c.hud.text_scale_q16=1u<<16;c.hud.opacity_q31=INT32_MAX;
 p.schema_version=ODM_LAYERED_SCHEMA_VERSION;p.width=64;p.height=64;p.fps=30;p.center_x_q16=32<<16;p.center_y_q16=32<<16;p.core_half_w_q16=8<<16;p.core_half_h_q16=8<<16;p.core_radius_q16=8<<16;p.field_opacity_q31=q(3,4);p.flags=ODM_COMPOSITION_FLAG_RADIAL_HIRES|ODM_COMPOSITION_FLAG_STRICT_CAUSAL|ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE|ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE;p.ring_phase=0u;
 tuple(&p,0u,UINT32_C(400000000),UINT32_C(500000000),UINT32_C(1600000000),UINT32_C(1000000000));
 tuple(&p,24u,UINT32_C(800000000),UINT32_C(200000000),UINT32_C(1200000000),UINT32_C(400000000));
 tuple(&p,48u,UINT32_C(100000000),UINT32_C(1000000000),UINT32_C(1800000000),UINT32_C(1500000000));
 f=(odm_layered_pixel16*)calloc((size_t)64u*64u,sizeof(*f));if(!f)return 3;odm_layered_field_render(&c,&p,f);out=fopen(av[1],"wb");if(!out){free(f);return 4;}if(fwrite(f,sizeof(*f),64u*64u,out)!=64u*64u){fclose(out);free(f);return 5;}if(fclose(out)!=0){free(f);return 6;}free(f);return 0;
}
'''

def mulq(a:int,b:int)->int:
    return min(Q,max(0,(a*b+Q//2)//Q))

def expected()->bytes:
    # g's raster functions use module globals W/H; they are 64x64 in that
    # independent oracle as well. Start transparent, matching calloc.
    frame=[(0,0,0,0) for _ in range(W*H)]
    center=32<<16; core_radius=8<<16; gap=1<<16; barmax=10<<16; width=1<<16
    for i in range(96):
        tup=TUPLES.get(i)
        if tup is None: continue
        body,release,final,attack=tup
        phase=((i<<32)//96)&g.MASK32
        cs=g.cosq(phase);sn=g.sinq(phase)
        r0=core_radius+gap
        ax=center+g.cdiv(r0*cs,Q); ay=center+g.cdiv(r0*sn,Q)
        body_len=(barmax*body+Q//2)//Q
        rb=r0+body_len
        bdx=center+g.cdiv(rb*cs,Q); bdy=center+g.cdiv(rb*sn,Q)
        # Layered Policy v11 optics (docs/RADIAL_MORPHOLOGY_V2.md section 4).
        if body_len>0:
            g.draw_segment(frame,ax,ay,bdx,bdy,width,SECONDARY,
                           spec.body_opacity(FIELD_OP,attack,release))
        release_base=spec.release_base(body,release)
        release_len=(barmax*release_base+Q//2)//Q
        rdx,rdy=bdx,bdy
        if release_len>body_len and release:
            rr=r0+release_len
            rdx=center+g.cdiv(rr*cs,Q);rdy=center+g.cdiv(rr*sn,Q)
            release_opacity=spec.release_opacity(FIELD_OP,release)
            if release_opacity:
                g.draw_segment(frame,bdx,bdy,rdx,rdy,width,SECONDARY,release_opacity)
        final_len=(barmax*final+Q//2)//Q
        if final_len>release_len and attack:
            rf=r0+final_len
            tx=center+g.cdiv(rf*cs,Q);ty=center+g.cdiv(rf*sn,Q)
            tip_opacity=spec.tip_opacity(FIELD_OP,attack)
            if tip_opacity:
                g.draw_segment(frame,rdx,rdy,tx,ty,width,PRIMARY,tip_opacity)
    return b''.join(struct.pack('<HHHH',*px) for px in frame)

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-multiscale-radial-raster-') as td0:
        td=pathlib.Path(td0);src=td/'probe.c';exe=td/'probe';raw=td/'field.bin';src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wsign-conversion','-Wshadow','-Werror','-I',str(root/'include'),'-I',str(root/'src/compositor'),'-I',str(root/'src/renderer'),'-I',str(root/'src/ir'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        c=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if c.returncode: raise SystemExit('multiscale radial raster probe compile failed\n'+c.stdout+c.stderr)
        r=subprocess.run([str(exe),str(raw)],cwd=root,capture_output=True,text=True)
        if r.returncode: raise SystemExit(f'multiscale radial raster probe failed rc={r.returncode}\n{r.stdout}\n{r.stderr}')
        got=raw.read_bytes()
    exp=expected()
    if got!=exp:
        lim=min(len(got),len(exp));j=next((i for i in range(lim) if got[i]!=exp[i]),lim);pix=j//8
        raise SystemExit(f'multiscale radial raster mismatch byte={j} pixel=({pix%W},{pix//W}) got={got[pix*8:pix*8+8].hex()} exp={exp[pix*8:pix*8+8].hex()}')
    print('MULTISCALE RADIAL RASTER ORACLE: PASS')
    print('  body: secondary/full field opacity; release: secondary/release-weighted opacity; attack: primary/attack-weighted tip')
    print('  three separated sectors reconstructed pixel-exact with 96-sector phase geometry')
    print('  stream sha256='+hashlib.sha256(exp).hexdigest())
    return 0
if __name__=='__main__': raise SystemExit(main())
