#!/usr/bin/env python3
"""Metamorphic oracle for Strict Causal visual rendering.

This oracle deliberately perturbs values that are forbidden to influence the
strict path (project tick/sample, composition phase/ring phase, Director orbit
phase/asymmetry, and field seed). Identical typed musical evidence must retain
identical Background+Field pixels. A non-strict control must remain sensitive
to those procedural authorities, proving the test is not vacuous.
"""
from __future__ import annotations
import argparse, hashlib, pathlib, subprocess, tempfile

PROBE = r'''#include "odm_compositor.h"
#include "odm_visual.h"
#include "compositor_internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 96u
#define H 96u
#define PIXELS ((size_t)W*(size_t)H)

static uint32_t qratio(uint32_t n,uint32_t d){
    return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);
}
static void fill_music(odm_composition_frame_state *c,uint64_t tick,uint32_t flags,uint32_t phase,uint32_t ring){
    uint32_t i;
    memset(c,0,sizeof(*c));
    c->schema_version=ODM_COMPOSITION_SCHEMA_VERSION;
    c->mode=ODM_COMPOSITION_MODE_FLOW;
    c->flags=flags;
    c->tick_index=tick;c->center_sample=tick*UINT64_C(480);
    c->phase=phase;c->ring_phase=ring;
    c->core_breath_q31=qratio(2u,5u);
    c->grid_q31=qratio(3u,5u);
    c->particles_q31=(uint32_t)INT32_MAX;
    c->radial_gain_q31=qratio(4u,5u);
    c->fracture_q31=qratio(1u,7u);
    c->accent_q31=qratio(2u,3u);
    for(i=0;i<ODM_COMPOSITION_RADIAL_SEGMENTS_MAX;++i){
        uint64_t v=UINT64_C(120000000)+(uint64_t)((i*2654435761u)&UINT32_C(0x3fffffff));
        if(v>(uint64_t)INT32_MAX)v=(uint64_t)INT32_MAX;
        c->radial_q31[i]=(uint32_t)v;
    }
}
static void fill_director(odm_director_frame_state *d,uint64_t tick,uint32_t orbit_phase,int32_t ax,int32_t ay){
    memset(d,0,sizeof(*d));
    d->schema_version=ODM_DIRECTOR_SCHEMA_VERSION;
    d->tick_index=tick;d->layout=ODM_DIRECTOR_LAYOUT_MONOLITH;
    d->previous_layout=ODM_DIRECTOR_LAYOUT_MONOLITH;
    d->transition_q31=(uint32_t)INT32_MAX;
    d->macro_energy_q31=qratio(1u,2u);
    d->macro_novelty_q31=qratio(1u,3u);
    d->depth_q31=qratio(2u,3u);
    d->edge_activity_q31=qratio(3u,4u);
    d->orbit_q31=qratio(2u,5u);
    d->asymmetry_x_q31=ax;d->asymmetry_y_q31=ay;d->orbit_phase=orbit_phase;
}
static int render_pair(const odm_layered_config *c,const odm_layered_frame_plan *p,odm_layered_pixel16 *bg,odm_layered_pixel16 *field){
    memset(bg,0,PIXELS*sizeof(*bg));memset(field,0,PIXELS*sizeof(*field));
    odm_layered_background_render(c,p,bg);
    odm_layered_field_render(c,p,field);
    return 0;
}
int main(int argc,char **argv){
    odm_layered_config a,b,legacy;
    odm_composition_frame_state ca,cb,na,nb,la,lb;
    odm_director_frame_state da,db,dna,dnb,dla,dlb;
    odm_layered_frame_plan pa,pb,pna,pnb,pla,plb;
    odm_layered_pixel16 *bga,*bgb,*fa,*fb,*nbga,*nbgb,*nfa,*nfb;
    FILE *f;
    if(argc!=2)return 2;
    if(odm_layered_config_init_default(&a,ODM_CANVAS_ASPECT_SQUARE_1_1,W,H,30)!=ODM_STATUS_OK)return 3;
    a.field.radial_segments=ODM_COMPOSITION_RADIAL_SEGMENTS_MAX;
    a.field.particle_count=48u;
    a.field.seed=UINT64_C(0x1111222233334444);
    if(odm_layered_config_validate(&a)!=ODM_STATUS_OK)return 4;
    b=a;b.field.seed=UINT64_C(0xddddeeeeffff0001);
    if(odm_layered_config_validate(&b)!=ODM_STATUS_OK)return 5;

    fill_music(&ca,100u,ODM_COMPOSITION_FLAG_RADIAL_HIRES|ODM_COMPOSITION_FLAG_STRICT_CAUSAL,
               UINT32_C(0x11111111),UINT32_C(0x22222222));
    fill_music(&cb,9000u,ODM_COMPOSITION_FLAG_RADIAL_HIRES|ODM_COMPOSITION_FLAG_STRICT_CAUSAL,
               UINT32_C(0xeeeeeeee),UINT32_C(0xfedcba98));
    fill_director(&da,ca.tick_index,UINT32_C(0x13579bdf),INT32_C(700000000),-INT32_C(600000000));
    fill_director(&db,cb.tick_index,UINT32_C(0xfdb97531),-INT32_C(500000000),INT32_C(650000000));
    if(odm_layered_resolve_frame_plan(&a,&ca,&da,INT64_C(48000),INT64_C(9000000),&pa)!=ODM_STATUS_OK)return 6;
    if(odm_layered_resolve_frame_plan(&b,&cb,&db,INT64_C(4320000),INT64_C(9000000),&pb)!=ODM_STATUS_OK)return 7;
    if(pa.flags!=(ODM_COMPOSITION_FLAG_RADIAL_HIRES|ODM_COMPOSITION_FLAG_STRICT_CAUSAL) ||
       pb.flags!=pa.flags || pa.ring_phase!=0u || pb.ring_phase!=0u ||
       pa.grid_offset_x_q16!=0 || pa.grid_offset_y_q16!=0 ||
       pb.grid_offset_x_q16!=0 || pb.grid_offset_y_q16!=0)return 8;
    /* Everything raster-relevant except forbidden time/identity metadata must match. */
    if(pa.center_x_q16!=pb.center_x_q16||pa.center_y_q16!=pb.center_y_q16||
       pa.core_radius_q16!=pb.core_radius_q16||pa.grid_spacing_q16!=pb.grid_spacing_q16||
       pa.grid_warp_q16!=pb.grid_warp_q16||pa.grid_depth_q16!=pb.grid_depth_q16||
       pa.field_opacity_q31!=pb.field_opacity_q31||pa.particle_count!=pb.particle_count||
       memcmp(pa.radial_q31,pb.radial_q31,sizeof(pa.radial_q31))!=0)return 9;

    bga=calloc(PIXELS,sizeof(*bga));bgb=calloc(PIXELS,sizeof(*bgb));fa=calloc(PIXELS,sizeof(*fa));fb=calloc(PIXELS,sizeof(*fb));
    nbga=calloc(PIXELS,sizeof(*nbga));nbgb=calloc(PIXELS,sizeof(*nbgb));nfa=calloc(PIXELS,sizeof(*nfa));nfb=calloc(PIXELS,sizeof(*nfb));
    if(!bga||!bgb||!fa||!fb||!nbga||!nbgb||!nfa||!nfb)return 10;
    render_pair(&a,&pa,bga,fa);render_pair(&b,&pb,bgb,fb);
    if(memcmp(bga,bgb,PIXELS*sizeof(*bga))!=0)return 11;
    if(memcmp(fa,fb,PIXELS*sizeof(*fa))!=0)return 12;

    /* Non-strict control: same musical values, but phase/ring/sample/seed are
     * valid authorities again. At least one Background and one Field byte must
     * differ, proving strict equality above is not caused by an inert fixture. */
    na=ca;nb=cb;na.flags=ODM_COMPOSITION_FLAG_RADIAL_HIRES;nb.flags=ODM_COMPOSITION_FLAG_RADIAL_HIRES;
    fill_director(&dna,na.tick_index,UINT32_C(0x13579bdf),0,0);
    fill_director(&dnb,nb.tick_index,UINT32_C(0xfdb97531),0,0);
    if(odm_layered_resolve_frame_plan(&a,&na,&dna,INT64_C(48000),INT64_C(9000000),&pna)!=ODM_STATUS_OK)return 13;
    if(odm_layered_resolve_frame_plan(&b,&nb,&dnb,INT64_C(4320000),INT64_C(9000000),&pnb)!=ODM_STATUS_OK)return 14;
    render_pair(&a,&pna,nbga,nfa);render_pair(&b,&pnb,nbgb,nfb);
    if(memcmp(nbga,nbgb,PIXELS*sizeof(*nbga))==0)return 15;
    if(memcmp(nfa,nfb,PIXELS*sizeof(*nfa))==0)return 16;

    /* Orthogonality: STRICT_CAUSAL must work without HIRES. */
    legacy=a;legacy.field.radial_segments=ODM_COMPOSITION_RADIAL_SEGMENTS;
    if(odm_layered_config_validate(&legacy)!=ODM_STATUS_OK)return 17;
    fill_music(&la,77u,ODM_COMPOSITION_FLAG_STRICT_CAUSAL,UINT32_C(0x12345678),UINT32_C(0x87654321));
    lb=la;lb.tick_index=177u;lb.center_sample=lb.tick_index*UINT64_C(480);lb.phase=UINT32_C(0xabcdef01);lb.ring_phase=UINT32_C(0x10fedcba);
    fill_director(&dla,la.tick_index,UINT32_C(0x11112222),0,0);fill_director(&dlb,lb.tick_index,UINT32_C(0xeeeeffff),0,0);
    if(odm_layered_resolve_frame_plan(&legacy,&la,&dla,INT64_C(36960),INT64_C(9000000),&pla)!=ODM_STATUS_OK)return 18;
    if(odm_layered_resolve_frame_plan(&legacy,&lb,&dlb,INT64_C(84960),INT64_C(9000000),&plb)!=ODM_STATUS_OK)return 19;
    if(pla.flags!=ODM_COMPOSITION_FLAG_STRICT_CAUSAL||plb.flags!=ODM_COMPOSITION_FLAG_STRICT_CAUSAL||
       pla.ring_phase!=0u||plb.ring_phase!=0u||pla.grid_offset_x_q16!=0||plb.grid_offset_x_q16!=0)return 20;

    f=fopen(argv[1],"wb");if(!f)return 21;
    fwrite(bga,sizeof(*bga),PIXELS,f);fwrite(fa,sizeof(*fa),PIXELS,f);
    fwrite(nbga,sizeof(*nbga),PIXELS,f);fwrite(nfa,sizeof(*nfa),PIXELS,f);
    fclose(f);
    free(bga);free(bgb);free(fa);free(fb);free(nbga);free(nbgb);free(nfa);free(nfb);
    return 0;
}
'''

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-strict-causal-oracle-') as td0:
        td=pathlib.Path(td0);src=td/'probe.c';exe=td/'probe';raw=td/'strict.bin';src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wsign-conversion','-Wshadow','-Werror',
             '-I',str(root/'include'),'-I',str(root/'src/compositor'),'-I',str(root/'src/renderer'),'-I',str(root/'src/ir'),
             str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        c=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if c.returncode: raise SystemExit('strict causal probe compile failed\n'+c.stdout+c.stderr)
        r=subprocess.run([str(exe),str(raw)],cwd=root,capture_output=True,text=True)
        if r.returncode: raise SystemExit(f'strict causal probe failed rc={r.returncode}\n{r.stdout}\n{r.stderr}')
        data=raw.read_bytes()
    frame_bytes=96*96*8
    if len(data)!=frame_bytes*4: raise SystemExit(f'unexpected evidence size {len(data)}')
    labels=['strict_background','strict_field','control_background','control_field']
    digests={labels[i]:hashlib.sha256(data[i*frame_bytes:(i+1)*frame_bytes]).hexdigest() for i in range(4)}
    print('STRICT CAUSAL ORACLE: PASS')
    print('  strict: identical Background+Field pixels across seed, sample/tick, composition phase/ring, Director orbit/asymmetry perturbations')
    print('  orthogonality: STRICT_CAUSAL verified independently of RADIAL_HIRES')
    print('  negative control: non-strict Background and Field remain sensitive to procedural authorities')
    for k,v in digests.items(): print(f'  {k} sha256={v}')
    return 0
if __name__=='__main__': raise SystemExit(main())
