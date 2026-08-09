#!/usr/bin/env python3
"""Independent oracle for multiscale radial provenance and coherent 96->48 reduction.

The engine is observed only through public APIs. Expected Q1.31 morphology is
reconstructed here from frozen literals: fast-body ceiling 0.45, exact
slow-fast release excess, release influence 0.35, then same-lane attack consumes
only remaining headroom. Reduction must select one complete tuple; it may not
mix body/release/attack from adjacent lanes.
"""
from __future__ import annotations
import argparse, pathlib, subprocess, tempfile

Q=2147483647
C045=966367642
C035=751619277
FLAG_HIRES=4
FLAG_STRICT=8
FLAG_PROV=16
FLAG_TIMESCALE=32
FLAG_TIMESCALE=32

PROBE=r'''#include "odm_compositor.h"
#include "odm_visual.h"
#include "odm_music_reaction.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int build_comp(odm_composition_frame_state *c){
    odm_music_analysis_tick b; odm_music_reaction_frame r;
    memset(&b,0,sizeof(b)); memset(&r,0,sizeof(r));
    b.tick_index=100u; b.center_sample=UINT64_C(48000); b.silence=0u;
    r.schema_version=ODM_MUSIC_REACTION_SCHEMA_VERSION;
    r.tick_index=b.tick_index; r.center_sample=b.center_sample;
    r.broadband_q31=UINT32_C(600000000);
    /* Pair 20: lane 40 has larger sustained evidence; lane 41 has a much
       stronger same-lane attack and therefore the larger final. */
    r.lane_q31[40]=UINT32_C(1400000000);
    r.lane_fast_q31[40]=UINT32_C(1100000000);
    r.lane_attack_q31[40]=UINT32_C(100000000);
    r.lane_q31[41]=UINT32_C(900000000);
    r.lane_fast_q31[41]=UINT32_C(300000000);
    r.lane_attack_q31[41]=UINT32_C(2000000000);
    return odm_composition_resolve_music_reaction(&b,&r,c)==ODM_STATUS_OK ? 0 : 1;
}
static void dir(odm_director_frame_state *d){
    memset(d,0,sizeof(*d)); d->schema_version=ODM_DIRECTOR_SCHEMA_VERSION;
    d->tick_index=100u; d->layout=ODM_DIRECTOR_LAYOUT_MONOLITH;
}
static int config(odm_layered_config *cfg,uint32_t segments){
    odm_status st=odm_layered_config_init_default(cfg,ODM_CANVAS_ASPECT_SQUARE_1_1,64u,64u,30);
    if(st!=ODM_STATUS_OK)return 1;
    cfg->field.radial_segments=segments;
    cfg->field.particle_count=0u;
    cfg->field.flags=ODM_FIELD_RADIAL_BARS;
    return odm_layered_config_validate(cfg)==ODM_STATUS_OK?0:2;
}
static int emit_plan(uint32_t segments){
    odm_composition_frame_state c; odm_director_frame_state d;
    odm_layered_config cfg; odm_layered_frame_plan p;
    uint32_t i,idx;
    if(build_comp(&c)||config(&cfg,segments))return 10;
    dir(&d);
    if(odm_layered_resolve_frame_plan(&cfg,&c,&d,48000,96000,&p)!=ODM_STATUS_OK)return 11;
    idx=segments==96u?40u:20u;
    printf("P,%u,%u," /* segments,flags */
           "%u,%u,%u,%u," /* lane40 */
           "%u,%u,%u,%u," /* lane41 */
           "%u,%u,%u,%u\n", /* chosen tuple */
      segments,p.flags,
      c.radial_q31[40],c.radial_body_q31[40],c.radial_release_q31[40],c.radial_attack_q31[40],
      c.radial_q31[41],c.radial_body_q31[41],c.radial_release_q31[41],c.radial_attack_q31[41],
      p.radial_q31[idx],p.radial_body_q31[idx],p.radial_release_q31[idx],p.radial_attack_q31[idx]);
    for(i=segments;i<ODM_COMPOSITION_RADIAL_SEGMENTS_MAX;i++){
      if(p.radial_q31[i]||p.radial_body_q31[i]||p.radial_release_q31[i]||p.radial_attack_q31[i])return 12;
    }
    if(segments==48u){
      uint64_t fb=0,sb=0; void *scratch=NULL; uint8_t *frame=NULL; odm_sha256_digest h;
      odm_layered_frame_plan bad=p; odm_status st;
      if(odm_layered_render_requirements(&cfg,ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,&fb,&sb)!=ODM_STATUS_OK)return 13;
      scratch=malloc((size_t)sb); frame=(uint8_t*)malloc((size_t)fb); if(!scratch||!frame){free(scratch);free(frame);return 14;}
      st=odm_layered_render_stack_rgba16(&cfg,&p,NULL,ODM_LAYERED_LAYER_MASK_FIELD,NULL,scratch,sb,frame,fb,&fb,&sb,&h);
      printf("R,%d,",(int)st);
      /* Forge release while leaving final/body/attack untouched. Render must
         reject the tuple because final is no longer derivable from provenance. */
      bad.radial_release_q31[20] = bad.radial_release_q31[20] > UINT32_C(1000000) ?
          bad.radial_release_q31[20]-UINT32_C(1000000) : UINT32_C(1000000);
      st=odm_layered_render_stack_rgba16(&cfg,&bad,NULL,ODM_LAYERED_LAYER_MASK_FIELD,NULL,scratch,sb,frame,fb,&fb,&sb,&h);
      printf("%d\n",(int)st);
      free(scratch);free(frame);
    }
    return 0;
}
int main(void){int rc=emit_plan(96u);if(rc)return rc;return emit_plan(48u);}
'''

def mulq(a:int,b:int)->int:
    return min(Q,(a*b+Q//2)//Q)

def morph(slow:int,fast:int,attack:int):
    if not 0 <= fast <= slow <= Q: raise ValueError((slow,fast,attack))
    body=mulq(fast,C045)
    release=slow-fast
    release_mix=mulq(release,C035)
    after_release=body+mulq(Q-body,release_mix)
    final=after_release+mulq(Q-after_release,attack)
    return final,body,release,attack

def main()->int:
    ap=argparse.ArgumentParser(); ap.add_argument('--root',required=True); ap.add_argument('--cc',default='gcc'); ap.add_argument('--library',required=True); a=ap.parse_args()
    root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-radial-prov-') as td0:
        td=pathlib.Path(td0); src=td/'probe.c'; exe=td/'probe'; src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        c=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if c.returncode: raise SystemExit('radial provenance probe compile failed\n'+c.stdout+c.stderr)
        x=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
        if x.returncode: raise SystemExit(f'radial provenance probe failed rc={x.returncode}\n{x.stdout}\n{x.stderr}')
    rows=[ln.split(',') for ln in x.stdout.splitlines() if ln.strip()]
    plans={int(r[1]):list(map(int,r[2:])) for r in rows if r[0]=='P'}
    a40=morph(1400000000,1100000000,100000000)
    a41=morph(900000000,300000000,2000000000)
    if a40[0] >= a41[0]: raise SystemExit(f'oracle fixture invalid: lane40 final {a40[0]} >= lane41 {a41[0]}')
    expected_flags=FLAG_HIRES|FLAG_STRICT|FLAG_PROV|FLAG_TIMESCALE
    for seg in (96,48):
        got=plans.get(seg)
        if got is None: raise SystemExit(f'missing plan {seg}')
        flags=got[0]
        if flags != expected_flags: raise SystemExit(f'{seg}: flags {flags} != {expected_flags}')
        c40=tuple(got[1:5]); c41=tuple(got[5:9]); chosen=tuple(got[9:13])
        if c40 != a40 or c41 != a41:
            raise SystemExit(f'{seg}: composition morphology mismatch c40={c40}/{a40} c41={c41}/{a41}')
        if seg==96:
            if chosen != a40: raise SystemExit(f'96: 1:1 provenance mismatch chosen={chosen} expected={a40}')
        else:
            if chosen != a41: raise SystemExit(f'48: tuple mixing or wrong winner got={chosen} expected lane41={a41}')
    rr=[list(map(int,r[1:])) for r in rows if r[0]=='R']
    if rr != [[0,12]]: raise SystemExit(f'render validation mismatch: {rr}; expected OK then INVALID_DATA(12)')
    print('RADIAL PROVENANCE ORACLE: PASS')
    print(f'  lane40 final/body/release/attack={a40}; lane41={a41}')
    print('  96 path: final/body/release/attack copied 1:1')
    print('  48 path: adjacent pair selects one complete winning tuple; no cross-lane morphology mixing')
    print('  render boundary: forged release inconsistent with final provenance is rejected as INVALID_DATA')
    return 0
if __name__=='__main__': raise SystemExit(main())
