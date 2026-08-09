#!/usr/bin/env python3
"""Independent oracle for Music-Reaction -> multi-axis visual causality v1.

The observed C engine is driven through a tiny public-API probe. Expected
control values are reconstructed here from frozen literal constants; this file
does not parse procedural.c or consume C policy bytes as expectations.
"""
from __future__ import annotations
import argparse, pathlib, subprocess, sys, tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import odm_radial_spec as _spec

Q=2147483647
C008=171798692; C010=214748365; C018=386547057; C020=429496730
C025=536870912; C035=751619277; C045=966367642; C048=1030792151
C055=1181116006; C058=1245540516; C070=1503238553; C080=1717986918
C085=1825361100; C090=1932735282
FLOW=2; IMPACT=3
FLAG_HIRES=4; FLAG_STRICT=8; FLAG_PROVENANCE=16; FLAG_TIMESCALE=32; FLAG_GLITCH=1

PROBE=r'''#include "odm_visual.h"
#include "odm_music_reaction.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void emit(const char *name,const odm_composition_frame_state *c){
    printf("%s %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",name,
      c->mode,c->flags,c->core_scale_q31,c->core_breath_q31,c->core_border_q31,
      c->halo_q31,c->grid_q31,c->particles_q31,c->memory_echo_q31,c->fracture_q31,
      c->chroma_split_q31,c->scan_q31,c->vignette_q31,c->void_q31,c->accent_q31,
      c->radial_q31[40],c->radial_q31[41]);
}
static int run_case(const char *name,uint32_t kind){
    odm_music_analysis_tick b; odm_music_reaction_frame r; odm_composition_frame_state c;
    memset(&b,0,sizeof(b)); memset(&r,0,sizeof(r));
    b.tick_index=42u;b.center_sample=42u*UINT64_C(480);b.silence=0u;
    r.schema_version=ODM_MUSIC_REACTION_SCHEMA_VERSION;r.tick_index=b.tick_index;r.center_sample=b.center_sample;
    r.broadband_q31=UINT32_C(600000000);
    r.lane_q31[40]=UINT32_C(500000000);r.lane_fast_q31[40]=UINT32_C(300000000);r.lane_attack_q31[40]=UINT32_C(200000000);
    if(kind==1u){r.family_q31[ODM_MUSIC_REACTION_FAMILY_SUB]=UINT32_C(800000000);r.family_attack_q31[ODM_MUSIC_REACTION_FAMILY_SUB]=UINT32_C(1000000000);}
    if(kind==2u){r.family_q31[ODM_MUSIC_REACTION_FAMILY_BODY]=UINT32_C(700000000);r.family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY]=UINT32_C(1000000000);}
    if(kind==3u){r.family_q31[ODM_MUSIC_REACTION_FAMILY_PRESENCE]=UINT32_C(700000000);r.family_attack_q31[ODM_MUSIC_REACTION_FAMILY_PRESENCE]=UINT32_C(1000000000);}
    if(kind==4u){r.family_q31[ODM_MUSIC_REACTION_FAMILY_AIR]=UINT32_C(700000000);r.family_attack_q31[ODM_MUSIC_REACTION_FAMILY_AIR]=UINT32_C(1000000000);r.event_strength_q31=UINT32_C(700000000);r.event_flags=ODM_MUSIC_REACTION_EVENT_ANY;r.frame_flags=ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE;r.macro_salience_q31=UINT32_C(1300000000);r.macro_pulse_q31=UINT32_C(1300000000);}
    if(kind==5u){r.event_strength_q31=UINT32_C(700000000);r.event_flags=ODM_MUSIC_REACTION_EVENT_ANY;r.frame_flags=ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE;r.macro_salience_q31=UINT32_C(1950000000);r.macro_pulse_q31=UINT32_C(1950000000);}
    if(odm_composition_resolve_music_reaction(&b,&r,&c)!=ODM_STATUS_OK)return 20+(int)kind;
    emit(name,&c);return 0;
}
static int director_case(const char *name,uint32_t strict,uint32_t radial){
    odm_composition_frame_state c; odm_director_state s; odm_director_frame_state f; uint32_t i;
    memset(&c,0,sizeof(c)); c.schema_version=ODM_COMPOSITION_SCHEMA_VERSION;c.mode=ODM_COMPOSITION_MODE_FLOW;
    c.flags=strict?ODM_COMPOSITION_FLAG_STRICT_CAUSAL|ODM_COMPOSITION_FLAG_RADIAL_HIRES:ODM_COMPOSITION_FLAG_RADIAL_HIRES;
    c.tick_index=0u;c.center_sample=0u;c.core_scale_q31=UINT32_C(1030792151);c.lateral_field_q31=UINT32_C(214748365);
    c.radial_gain_q31=UINT32_C(2147483647);for(i=0u;i<ODM_COMPOSITION_RADIAL_SEGMENTS_MAX;++i)c.radial_q31[i]=radial;
    if(odm_director_init(&s,UINT64_C(0x123456789abcdef0))!=ODM_STATUS_OK)return 40;
    if(odm_director_resolve_tick(&s,&c,&f)!=ODM_STATUS_OK)return 41;
    printf("DIR %s %u %u\n",name,f.layout,f.orbit_q31);return 0;
}
int main(void){int rc=0;rc|=run_case("base",0u);rc|=run_case("low",1u);rc|=run_case("mid",2u);rc|=run_case("presence",3u);rc|=run_case("air_context",4u);rc|=run_case("exceptional",5u);rc|=director_case("strict_quiet",1u,0u);rc|=director_case("strict_active",1u,UINT32_C(1500000000));rc|=director_case("legacy_gain",0u,0u);return rc;}
'''

def sat(v:int)->int:return min(Q,max(0,v))
def mul(a:int,b:int)->int:return sat((a*b+Q//2)//Q)
def add(a:int,b:int)->int:return sat(a+b)
def lerp(lo:int,hi:int,t:int)->int:return add(lo,mul(hi-lo,t))
def gate(v:int,t:int)->int:
    if t>=Q or v<=t:return 0
    if v>=Q:return Q
    den=Q-t
    return sat(((v-t)*Q+den//2)//den)

def expected(kind:int):
    fam=[0]*6; atk=[0]*6; broad=600000000; battack=0; sal=0; pulse=0
    if kind==1:fam[0]=800000000;atk[0]=1000000000
    if kind==2:fam[3]=700000000;atk[3]=1000000000
    if kind==3:fam[4]=700000000;atk[4]=1000000000
    if kind==4:fam[5]=700000000;atk[5]=1000000000;sal=pulse=1300000000
    if kind==5:sal=pulse=1950000000
    low=max(fam[0],fam[1]); lowa=max(atk[0],atk[1]); mid=max(fam[2],fam[3]); mida=max(atk[2],atk[3]); higha=max(atk[4],atk[5])
    ag=gate(sal,C020); ig=gate(sal,C055); sg=gate(sal,C080); mg=gate(pulse,C035)
    mode=IMPACT if sal>=C055 else FLOW
    flags=FLAG_HIRES|FLAG_STRICT|FLAG_PROVENANCE|FLAG_TIMESCALE|(FLAG_GLITCH if sal>=C085 else 0)
    core_scale=lerp(C048,C058,low)
    # Visual Policy v9 timescale separation. Core breath is a punch plus a
    # causal bleed-back: low-frequency attack owns the fast expansion, and
    # contextual pulse memory owns a smaller, slower return tail — a MAX, so the
    # tail cannot stack on top of the punch. Grid is a scene-timescale response
    # driven by sustained mid/body level plus a small memory term; same-tick mid
    # attack is deliberately absent so the background cannot whip at Halo speed
    # on every onset.
    core_breath=max(mul(lowa,C035),mul(mg,C018))
    core_border=add(mul(lowa,C025),mul(ag,C018))
    halo=max(battack,add(mul(higha,C035),mul(mg,C025)))
    grid=add(mul(mid,C025),mul(mg,C008))
    particles=higha
    memory=mul(mg,C055)
    fracture=mul(sg,C070)
    chroma=mul(atk[5],ig)
    scan=mul(fam[5],C018)
    vignette=lerp(C055,C070,Q-fam[4])
    void=mul(C018,Q-broad)
    accent=max(atk[4],mul(ig,C070))
    # Radial Morphology v2 / Visual Policy v9 — see docs/RADIAL_MORPHOLOGY_V2.md.
    radial40=_spec.morphology(500000000,300000000,200000000)[0]; radial41=0
    return [mode,flags,core_scale,core_breath,core_border,halo,grid,particles,memory,fracture,chroma,scan,vignette,void,accent,radial40,radial41]

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve(); lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-multi-axis-') as td0:
        td=pathlib.Path(td0); src=td/'probe.c'; exe=td/'probe'; src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wsign-conversion','-Wshadow','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        c=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if c.returncode: raise SystemExit('probe compile failed\n'+c.stdout+c.stderr)
        r=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
        if r.returncode: raise SystemExit(f'probe failed rc={r.returncode}\n{r.stdout}\n{r.stderr}')
    names=['base','low','mid','presence','air_context','exceptional']; lines=r.stdout.strip().splitlines()
    visual_lines=[x for x in lines if not x.startswith('DIR ')]
    dir_lines=[x for x in lines if x.startswith('DIR ')]
    if len(visual_lines)!=len(names) or len(dir_lines)!=3:raise SystemExit(f'unexpected probe lines visual={len(visual_lines)} dir={len(dir_lines)}')
    observed={}
    for line in visual_lines:
        parts=line.split(); observed[parts[0]]=[int(x) for x in parts[1:]]
    director={}
    for line in dir_lines:
        parts=line.split(); director[parts[1]]=[int(parts[2]),int(parts[3])]
    for i,name in enumerate(names):
        exp=expected(i); got=observed.get(name)
        if got!=exp:
            diffs=[(j,g,e) for j,(g,e) in enumerate(zip(got or [],exp)) if g!=e]
            raise SystemExit(f'{name}: mismatch {diffs[:8]} got={got} expected={exp}')
    # Metamorphic authority checks: isolated spectral families must not drag unrelated global axes.
    b=observed['base']; low=observed['low']; mid=observed['mid']; pre=observed['presence']; exc=observed['exceptional']
    # indexes: mode flags scale breath border halo grid particles memory fracture chroma scan vignette void accent radial40 radial41
    if not (low[3]>b[3] and low[4]>b[4] and low[6]==b[6] and low[7]==b[7] and low[9]==0): raise SystemExit('low-axis isolation failed')
    if not (mid[6]>b[6] and mid[3]==b[3] and mid[7]==b[7] and mid[9]==0): raise SystemExit('mid-axis isolation failed')
    if not (pre[7]>b[7] and pre[14]>b[14] and pre[6]==b[6] and pre[9]==0): raise SystemExit('high-axis isolation failed')
    if not (exc[9]>0 and exc[7]==0 and exc[10]==0 and (exc[1]&FLAG_GLITCH)!=0): raise SystemExit('structural authority isolation failed')
    if len({observed[n][15] for n in names})!=1 or any(observed[n][16]!=0 for n in names): raise SystemExit('macro/family axes contaminated local radial evidence')
    # Director negative/positive control: strict path ignores unity transfer gain
    # and derives orbit candidacy from actual visible radial evidence. Legacy
    # remains gain-sensitive by design. Layout ids: MONOLITH=0, ORBIT=1.
    if director.get('strict_quiet',[99])[0]!=0: raise SystemExit(f"strict quiet falsely promoted by radial_gain: {director}")
    if director.get('strict_active',[99])[0]!=1: raise SystemExit(f"strict active radials did not promote ORBIT: {director}")
    if director.get('legacy_gain',[99])[0]!=1: raise SystemExit(f"legacy gain negative control lost: {director}")
    if not (director['strict_active'][1] > director['strict_quiet'][1]): raise SystemExit('strict orbit intensity did not follow radial activity')
    print('MULTI-AXIS VISUAL CAUSALITY ORACLE: PASS')
    print('  low attack -> Core breath/border; mid/body -> Grid; presence/air -> particles/accent; exceptional contextual salience -> structural fracture')
    print('  moderate salience cannot fracture; local radial evidence is byte-identical across macro-authority perturbations')
    print('  strict Director: unity radial gain cannot masquerade as activity; 96-radial mean drives ORBIT, with legacy gain as negative control')
    print('  six synthetic authority-isolation cases reconstructed integer-exact from independent Q1.31 formulas')
    return 0
if __name__=='__main__': raise SystemExit(main())
