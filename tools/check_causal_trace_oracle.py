#!/usr/bin/env python3
"""Independent oracle for reaction evidence + post-compositor radial trace closure.

The reaction API must expose slow, fast and attack evidence without pretending the
legacy 48-pair summary is a selected compositor tuple. The compositor trace must
then re-derive the exact 96-native or 96->48 winning lane from Composition and
cross-check final/body/release/attack against the published Frame Plan.
"""
from __future__ import annotations
import argparse, pathlib, subprocess, tempfile

Q=2147483647
C045=966367642
C035=751619277

PROBE=r'''#include "odm_compositor.h"
#include "odm_music_reaction.h"
#include "odm_visual.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fixture(odm_music_reaction_frame *r, odm_composition_frame_state *c){
  odm_music_analysis_tick b; memset(&b,0,sizeof(b)); memset(r,0,sizeof(*r));
  b.tick_index=100u;b.center_sample=UINT64_C(48000);b.silence=0u;
  r->schema_version=ODM_MUSIC_REACTION_SCHEMA_VERSION;r->tick_index=b.tick_index;r->center_sample=b.center_sample;
  r->broadband_q31=UINT32_C(600000000);
  r->lane_q31[40]=UINT32_C(1400000000);r->lane_fast_q31[40]=UINT32_C(1100000000);r->lane_attack_q31[40]=UINT32_C(100000000);
  r->lane_q31[41]=UINT32_C(900000000);r->lane_fast_q31[41]=UINT32_C(300000000);r->lane_attack_q31[41]=UINT32_C(2000000000);
  return odm_composition_resolve_music_reaction(&b,r,c)==ODM_STATUS_OK?0:1;
}
static int config(odm_layered_config *cfg,uint32_t n){
  odm_status st=odm_layered_config_init_default(cfg,ODM_CANVAS_ASPECT_SQUARE_1_1,64u,64u,30);
  if(st!=ODM_STATUS_OK)return 1;
  cfg->field.radial_segments=n;cfg->field.flags=ODM_FIELD_RADIAL_BARS;cfg->field.particle_count=0u;
  return odm_layered_config_validate(cfg)==ODM_STATUS_OK?0:2;
}
static void director(odm_director_frame_state *d){memset(d,0,sizeof(*d));d->schema_version=ODM_DIRECTOR_SCHEMA_VERSION;d->tick_index=100u;d->layout=ODM_DIRECTOR_LAYOUT_MONOLITH;}
int main(void){
  odm_music_reaction_frame r;odm_composition_frame_state c;odm_music_reaction_trace rt;
  odm_layered_config c96,c48;odm_director_frame_state d;odm_layered_frame_plan p96,p48,badp;odm_composition_frame_state badc;odm_layered_radial_trace lt;odm_status st;
  if(fixture(&r,&c)||config(&c96,96u)||config(&c48,48u))return 2;
  director(&d);
  if(odm_music_reaction_trace_radial(&r,20u,&rt)!=ODM_STATUS_OK)return 3;
  printf("R,%u,%u,%u,%u,%u,%u\n",rt.lane_a,rt.lane_b,rt.direct_level_q31,rt.direct_fast_level_q31,rt.direct_attack_q31,(unsigned)sizeof(rt));
  if(odm_music_reaction_trace_hires_radial(&r,41u,&rt)!=ODM_STATUS_OK)return 4;
  printf("H,%u,%u,%u,%u,%u\n",rt.lane_a,rt.lane_b,rt.direct_level_q31,rt.direct_fast_level_q31,rt.direct_attack_q31);
  if(odm_layered_resolve_frame_plan(&c96,&c,&d,48000,96000,&p96)!=ODM_STATUS_OK)return 5;
  if(odm_layered_resolve_frame_plan(&c48,&c,&d,48000,96000,&p48)!=ODM_STATUS_OK)return 6;
  if(odm_layered_trace_radial(&c96,&c,&p96,40u,&lt)!=ODM_STATUS_OK)return 7;
  printf("N,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",lt.radial_index,lt.output_radial_count,lt.source_lane,lt.candidate_lane_a,lt.candidate_lane_b,lt.flags,lt.final_q31,lt.body_q31,lt.release_q31,lt.attack_q31);
  if(odm_layered_trace_radial(&c48,&c,&p48,20u,&lt)!=ODM_STATUS_OK)return 8;
  printf("D,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",lt.radial_index,lt.output_radial_count,lt.source_lane,lt.candidate_lane_a,lt.candidate_lane_b,lt.flags,lt.final_q31,lt.body_q31,lt.release_q31,lt.attack_q31);
  badp=p48;badp.radial_body_q31[20]^=1u;st=odm_layered_trace_radial(&c48,&c,&badp,20u,&lt);printf("P,%d\n",(int)st);
  badc=c;badc.radial_q31[40]=UINT32_C(2147483647);st=odm_layered_trace_radial(&c48,&badc,&p48,20u,&lt);printf("C,%d\n",(int)st);
  c48.hud.opacity_q31=UINT32_C(1000000000);st=odm_layered_trace_radial(&c48,&c,&p48,20u,&lt);printf("G,%d\n",(int)st);
  return 0;
}
'''

def mulq(a:int,b:int)->int:return min(Q,(a*b+Q//2)//Q)
import sys as _sys
_sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import odm_radial_spec as _spec

# Radial Morphology v2 / Visual Policy v9. Reconstructed independently in
# tools/odm_radial_spec.py from docs/RADIAL_MORPHOLOGY_V2.md.
morph = _spec.morphology

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-causal-trace-') as td0:
        td=pathlib.Path(td0);src=td/'probe.c';exe=td/'probe';src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        c=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if c.returncode:raise SystemExit('causal trace probe compile failed\n'+c.stdout+c.stderr)
        x=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
        if x.returncode:raise SystemExit(f'causal trace probe failed rc={x.returncode}\n{x.stdout}\n{x.stderr}')
    rows={r[0]:list(map(int,r[1:])) for r in (ln.split(',') for ln in x.stdout.splitlines()) if r}
    a40=morph(1400000000,1100000000,100000000);a41=morph(900000000,300000000,2000000000)
    if not a41[0]>a40[0]:raise SystemExit('fixture winner assumption invalid')
    # Legacy reaction trace is explicitly only a two-lane evidence summary.
    if rows.get('R')[:5] != [40,41,1150000000,700000000,2000000000]:raise SystemExit(f'reaction pair trace mismatch {rows.get("R")}')
    if rows.get('R')[5] != 64:raise SystemExit(f'reaction trace ABI size changed unexpectedly: {rows.get("R")[5]}')
    if rows.get('H') != [41,41,900000000,300000000,2000000000]:raise SystemExit(f'hires trace mismatch {rows.get("H")}')
    native=[40,96,40,40,40,1|4|8,*a40]
    reduced=[20,48,41,40,41,2|4|8,*a41]
    if rows.get('N') != native:raise SystemExit(f'native compositor trace mismatch got={rows.get("N")} exp={native}')
    if rows.get('D') != reduced:raise SystemExit(f'reduced compositor trace mismatch got={rows.get("D")} exp={reduced}')
    for tag in ('P','C','G'):
        if rows.get(tag) != [12]:raise SystemExit(f'{tag} tamper not rejected: {rows.get(tag)}')
    print('CAUSAL TRACE ORACLE: PASS')
    print('  reaction trace: slow + fast + attack exposed; legacy 48 trace remains explicitly a candidate-pair summary')
    print(f'  native 96: radial 40 -> lane 40 tuple={a40}')
    print(f'  reduced 48: radial 20 candidates 40/41 -> exact winning lane 41 tuple={a41}')
    print('  forged plan tuple, changed winner composition, and mismatched config identity: all rejected as INVALID_DATA')
    print('  reaction trace size remains 64 bytes while former reserved slot becomes fast-envelope evidence')
    return 0
if __name__=='__main__':raise SystemExit(main())
