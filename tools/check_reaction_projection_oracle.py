#!/usr/bin/env python3
"""Independent oracle for causal 100 Hz -> presentation-rate projection.

The expected interval semantics are reconstructed in Python.  The C engine is
only the observed implementation.  This specifically guards against temporal
aliasing: instantaneous reaction evidence between video frames must be captured
exactly once, never anticipated, at 24/25/30/50/60/100/120 fps.
"""
from __future__ import annotations
import argparse
import pathlib
import subprocess
import tempfile

TICK_SAMPLES = 480
FPS_STEPS = [(24,2000),(25,1920),(30,1600),(50,960),(60,800),(100,480),(120,400)]
FRAME_COUNT = 101  # tick 0 .. tick 100, centers 0 .. 48000 inclusive

PROBE = r'''#include "odm_music_reaction.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void init_frame(odm_music_reaction_frame *f, uint32_t i) {
  memset(f, 0, sizeof(*f));
  f->schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
  f->tick_index = i;
  f->center_sample = (uint64_t)i * UINT64_C(480);
  f->lane_q31[7] = UINT32_C(1000000) + i;
  f->family_q31[ODM_MUSIC_REACTION_FAMILY_BODY] = UINT32_C(2000000) + i;
  f->broadband_q31 = UINT32_C(3000000) + i;
  if ((i % 7u) == 3u) f->lane_attack_q31[7] = UINT32_C(1500000000) - i;
  if ((i % 11u) == 5u) f->family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY] = UINT32_C(1200000000) - i;
  if ((i % 13u) == 6u) f->broadband_attack_q31 = UINT32_C(1300000000) - i;
  if (i == 17u || i == 66u || i == 99u) {
    f->event_strength_q31 = UINT32_C(900000000) + i;
    f->event_pulse_q31 = UINT32_C(910000000) + i;
    f->event_flags = ODM_MUSIC_REACTION_EVENT_ANY | ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
  }
}

int main(void) {
  static const uint32_t fps[7] = {24u,25u,30u,50u,60u,100u,120u};
  static const uint32_t step[7] = {2000u,1920u,1600u,960u,800u,480u,400u};
  odm_music_reaction_frame frames[101];
  odm_music_reaction_projection out[128];
  uint64_t samples[128];
  uint32_t i, k;
  for (i=0u;i<101u;++i) init_frame(&frames[i], i);
  for (k=0u;k<7u;++k) {
    uint32_t n=0u;
    uint64_t s=0u;
    while (s <= UINT64_C(48000)) {
      samples[n++] = s;
      if (s == UINT64_C(48000)) break;
      s += step[k];
      if (s > UINT64_C(48000)) s = UINT64_C(48000);
    }
    if (odm_music_reaction_project_timeline(frames,101u,samples,n,out,128u) != ODM_STATUS_OK) return 2;
    printf("F,%u,%u\n",fps[k],n);
    for (i=0u;i<n;++i) {
      printf("P,%u,%u,%llu,%llu,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
        fps[k], i,
        (unsigned long long)samples[i],
        (unsigned long long)out[i].source_tick_index,
        (unsigned long long)out[i].source_center_sample,
        out[i].captured_tick_count,
        out[i].captured_event_tick_count,
        out[i].lane_q31[7],
        out[i].lane_attack_q31[7],
        out[i].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY],
        out[i].broadband_attack_q31,
        out[i].event_strength_q31,
        out[i].event_pulse_q31,
        out[i].event_flags);
    }
  }
  {
    odm_music_reaction_frame ctx[101];
    odm_music_reaction_projection cp;
    memcpy(ctx,frames,sizeof(ctx));
    for (i=0u;i<101u;++i) ctx[i].frame_flags=ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE;
    ctx[17].macro_salience_q31=UINT32_C(1200000000);
    ctx[17].macro_pulse_q31=UINT32_C(1200000000);
    ctx[18].macro_pulse_q31=UINT32_C(960000000);
    if (odm_music_reaction_project_presentation(ctx,101u,INT64_C(7680),UINT64_C(8640),&cp) != ODM_STATUS_OK) return 3;
    printf("C,%llu,%u,%u,%u,%u,%u,%u\n",
      (unsigned long long)cp.source_tick_index,cp.captured_tick_count,cp.captured_event_tick_count,
      cp.event_strength_q31,cp.macro_salience_q31,cp.macro_pulse_q31,cp.frame_flags);
    ctx[50].frame_flags=0u;
    printf("M,%d\n",(int)odm_music_reaction_project_presentation(ctx,101u,INT64_C(7680),UINT64_C(8640),&cp));
  }
  {
    uint64_t bad[3]={0u,2000u,1900u};
    odm_music_reaction_projection sent[3];
    unsigned char *p=(unsigned char*)sent;
    uint64_t j; int unchanged=1;
    memset(sent,0x5a,sizeof(sent));
    printf("B,%d,", (int)odm_music_reaction_project_timeline(frames,101u,bad,3u,sent,3u));
    for (j=0u;j<(uint64_t)sizeof(sent);++j) if (p[j] != 0x5au) { unchanged=0; break; }
    printf("%d\n",unchanged);
  }
  return 0;
}
'''

def frame(i:int)->dict[str,int]:
    return {
        'lane': 1_000_000+i,
        'lane_attack': 1_500_000_000-i if i%7==3 else 0,
        'family_attack': 1_200_000_000-i if i%11==5 else 0,
        'broadband_attack': 1_300_000_000-i if i%13==6 else 0,
        'event_strength': 900_000_000+i if i in (17,66,99) else 0,
        'event_pulse': 910_000_000+i if i in (17,66,99) else 0,
        'event_flags': (64 | 128) if i in (17,66,99) else 0,
    }

def max_field(begin:int,end:int,key:str)->int:
    if begin>end:return 0
    return max(frame(i)[key] for i in range(begin,end+1))

def main()->int:
    ap=argparse.ArgumentParser(); ap.add_argument('--root',required=True); ap.add_argument('--cc',default='gcc'); ap.add_argument('--library',required=True); a=ap.parse_args()
    root=pathlib.Path(a.root).resolve(); lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-reaction-projection-oracle-') as td0:
        td=pathlib.Path(td0); src=td/'probe.c'; exe=td/'probe'; src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        c=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if c.returncode: raise SystemExit('reaction projection probe compile failed\n'+c.stdout+c.stderr)
        r=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
        if r.returncode: raise SystemExit(f'reaction projection probe failed rc={r.returncode}\n{r.stdout}\n{r.stderr}')
    rows=[ln.split(',') for ln in r.stdout.splitlines() if ln.strip()]
    byfps={fps:[] for fps,_ in FPS_STEPS}
    declared={}
    bad=None; contextual=None; mixed=None
    for row in rows:
        if row[0]=='F': declared[int(row[1])]=int(row[2])
        elif row[0]=='P': byfps[int(row[1])].append(tuple(map(int,row[2:])))
        elif row[0]=='C': contextual=tuple(map(int,row[1:]))
        elif row[0]=='M': mixed=int(row[1])
        elif row[0]=='B': bad=(int(row[1]),int(row[2]))
    for fps,step in FPS_STEPS:
        obs=byfps[fps]
        if declared.get(fps)!=len(obs): raise SystemExit(f'{fps} fps row count mismatch')
        captured_total=0; captured_events=0; prev=-1
        for row in obs:
            (idx,sample,source_tick,source_center,captured_count,event_count,lane,lane_attack,
             family_attack,broadband_attack,event_strength,event_pulse,event_flags)=row
            exp_source=sample//TICK_SAMPLES
            if source_tick!=exp_source or source_center!=exp_source*TICK_SAMPLES:
                raise SystemExit(f'{fps} fps source mismatch at presentation {idx}')
            # Batch seek semantics: first presentation sees only its exact source tick
            # when sample=0; thereafter interval is (prev,current].
            begin=0 if prev<0 else prev//TICK_SAMPLES+1
            end=exp_source
            exp_count=max(0,end-begin+1)
            event_ticks=[i for i in range(begin,end+1) if frame(i)['event_strength']] if begin<=end else []
            if captured_count!=exp_count: raise SystemExit(f'{fps} fps captured-count mismatch at {idx}: {captured_count}!={exp_count}')
            if event_count!=len(event_ticks): raise SystemExit(f'{fps} fps event-count mismatch at {idx}')
            if lane!=frame(exp_source)['lane']: raise SystemExit(f'{fps} fps held lane mismatch at {idx}')
            expected=(max_field(begin,end,'lane_attack'),max_field(begin,end,'family_attack'),
                      max_field(begin,end,'broadband_attack'),max_field(begin,end,'event_strength'),
                      frame(exp_source)['event_pulse'])
            got=(lane_attack,family_attack,broadband_attack,event_strength,event_pulse)
            if got!=expected: raise SystemExit(f'{fps} fps transient aggregation mismatch at {idx}: got={got} expected={expected}')
            exp_flags=0
            for i in event_ticks: exp_flags |= frame(i)['event_flags']
            if event_flags!=exp_flags: raise SystemExit(f'{fps} fps event flags mismatch at {idx}')
            # Non-anticipation: the strongest future event at tick 99 cannot appear before its center.
            if sample < 99*TICK_SAMPLES and event_strength >= 900_000_099:
                raise SystemExit(f'{fps} fps future event leaked at sample {sample}')
            captured_total += captured_count; captured_events += event_count; prev=sample
        if captured_total!=FRAME_COUNT: raise SystemExit(f'{fps} fps lost/duplicated ticks: {captured_total} != {FRAME_COUNT}')
        if captured_events!=3: raise SystemExit(f'{fps} fps lost/duplicated events: {captured_events} != 3')
        if obs[-1][1] != 48000: raise SystemExit(f'{fps} fps schedule did not close at 48000')
    expected_contextual=(18,2,1,900_000_017,1_200_000_000,960_000_000,1)
    if contextual != expected_contextual:
        raise SystemExit(f'contextual projection mismatch got={contextual} expected={expected_contextual}')
    # ODM_STATUS_INVALID_DATA == 12: one projection stream cannot mix evidence-only
    # and contextual frames because macro authority would become interval-dependent.
    if mixed != 12:
        raise SystemExit(f'mixed contextual/evidence stream was not rejected: {mixed}')
    if bad is None or bad[1]!=1:
        raise SystemExit(f'transactional invalid-schedule output was modified: {bad}')
    print('REACTION PRESENTATION PROJECTION ORACLE: PASS')
    print('  24/25/30/50/60/100/120 fps: every 100 Hz source tick captured exactly once')
    print('  instantaneous lane/family/broadband attacks: interval-max preserved')
    print('  event flags/strength + contextual macro salience: interval-captured; raw/macro pulses: held from causal source tick')
    print('  mixed evidence-only/contextual stream: rejected before projection')
    print('  invalid non-monotonic schedule: transactional output preserved')
    return 0
if __name__=='__main__': raise SystemExit(main())
