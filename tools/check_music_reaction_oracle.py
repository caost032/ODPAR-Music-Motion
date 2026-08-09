#!/usr/bin/env python3
"""Independent oracle for ODPAR Music Reaction Spine v1.

The C engine is the observed implementation. Expected lane topology, robust
order statistics, locality, frequency-aware release, offline peak refinement, and time/phase isolation are reconstructed here.
"""
from __future__ import annotations
import argparse, hashlib, pathlib, struct, subprocess, tempfile

LANE_EDGES = [
1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,
27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,
51,52,53,54,55,56,57,58,59,60,64,68,73,78,84,90,96,103,111,119,127,136,
146,156,168,180,192,206,221,237,254,272,291,312,335,359,384,412,441,473,
507,543,582,624,669,717,768]
FAMILY_EDGES = [1,4,9,26,107,342,768]
Q = 2147483647
POLICY_BYTES = 576

def expected_policy_bytes()->bytes:
    # Independent literal reconstruction of Music-Reaction Policy v11.
    b=bytearray(b'ODMRXP11')
    def u16(v:int)->None: b.extend(struct.pack('<H',v))
    def u32(v:int)->None: b.extend(struct.pack('<I',v))
    for v in (11,2,96,6,100,480,2048,99,100,95,100,3,4,1,2,1503238553): u32(v)
    # Slow release by family, then fast release by family.
    for v in (12,10,8,7,5,4): u32(v)
    for v in (4,4,3,3,2,2): u32(v)
    # Broadband/event/projection contract.
    for v in (8,2,644245094,32,14,0,5,2,128,
              2,48000,1,1,1,1,50,100,2,2,1,
              10,100): u32(v)
    for v in (10,20,30,40,50,60,70,80,90,99): u32(v)
    # Contextual salience + event-record provenance.
    for v in (214748365,1,2,2,1,128,240,1,1,1): u32(v)
    # Multiscale lane contract: slow + fast + attack, same-lane only, fast<=slow.
    for v in (3,1,1): u32(v)
    # Offline sample-domain event presentation: schema/size, confidence gate,
    # 140 ms pulse domain, weak-refinement fallback, half-open capture,
    # sample-domain linear decay, max-decayed-salience ownership.
    for v in (1,128,268435456,6720,1,1,1,1,1): u32(v)
    for v in LANE_EDGES: u16(v)
    for v in FAMILY_EDGES: u16(v)
    if len(b)>POLICY_BYTES: raise AssertionError(len(b))
    b.extend(b'\0'*(POLICY_BYTES-len(b)))
    return bytes(b)



PROBE = r'''#include "odm_music_reaction.h"
#include "odm_visual.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void init_tick(odm_music_reaction_tick *t, uint64_t i) {
  memset(t, 0, sizeof(*t));
  t->schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
  t->tick_index = i; t->center_sample = i * UINT64_C(480);
}
static void hx(const uint8_t *p, uint64_t n) { uint64_t i; for(i=0;i<n;i++) printf("%02x",p[i]); }
int main(void) {
  odm_music_analysis_tick base, base2;
  odm_music_analysis_scratch scratch;
  odm_music_reaction_tick ext, ticks[220];
  odm_music_reaction_profile p;
  odm_music_reaction_frame r, r2, rf0, rf1, ofin[40], ofout[40];
  odm_composition_frame_state c, c2;
  odm_music_reaction_trace tr;
  odm_music_reaction_event_trace et; odm_music_reaction_frame ev;
  odm_music_reaction_profile rp, hp;
  odm_music_reaction_state rs, hs;
  odm_music_reaction_tick rt0, rt1, ht0, ht1;
  odm_music_reaction_frame hf0, hf1, pf[20];
  odm_music_reaction_projection pq[32], seekp;
  uint64_t ps[32];
  uint32_t i, a, b;
  uint8_t policy[ODM_MUSIC_REACTION_POLICY_BYTES]; uint64_t preq=0; odm_sha256_digest ph;
  if(odm_music_reaction_policy_bytes(policy,sizeof(policy),&preq)!=ODM_STATUS_OK || preq!=sizeof(policy))return 13;
  if(odm_music_reaction_policy_current_sha256(&ph)!=ODM_STATUS_OK)return 14;
  printf("K,");hx(policy,sizeof(policy));putchar('\n');printf("J,");hx(ph.bytes,32);putchar('\n');
  memset(&base,0,sizeof(base)); memset(&scratch,0,sizeof(scratch));
  base.tick_index=7; base.center_sample=3360;
  for(i=0;i<ODM_MUSIC_REACTION_LANE_COUNT;i++){
    if(odm_music_reaction_lane_bounds(i,&a,&b)!=ODM_STATUS_OK)return 2;
    printf("E,%u,%u,%u\n",i,a,b);
  }
  scratch.magnitude_q31[10]=(uint32_t)INT32_MAX;
  if(odm_music_reaction_extract_tick(&base,&scratch,&ext)!=ODM_STATUS_OK)return 3;
  for(i=0;i<ODM_MUSIC_REACTION_LANE_COUNT;i++) if(ext.lane_magnitude_q31[i])
    printf("X,%u,%u\n",i,ext.lane_magnitude_q31[i]);
  for(i=0;i<220;i++){
    uint32_t v=UINT32_C(1000000),j;
    if((i%10u)==1u)v=UINT32_C(1100000);
    if(i==219u)v=(uint32_t)INT32_MAX;
    init_tick(&ticks[i],i);
    for(j=0;j<ODM_MUSIC_REACTION_LANE_COUNT;j++)
      ticks[i].lane_magnitude_q31[j]=(v>(uint32_t)INT32_MAX-j)?(uint32_t)INT32_MAX:v+j;
    for(j=0;j<ODM_MUSIC_REACTION_FAMILY_COUNT;j++)
      ticks[i].family_magnitude_q31[j]=(v>(uint32_t)INT32_MAX-j)?(uint32_t)INT32_MAX:v+j;
    ticks[i].broadband_magnitude_q31=v;
  }
  if(odm_music_reaction_profile_build(ticks,220,&p)!=ODM_STATUS_OK)return 4;
  printf("P,%u,%u,%u,%u\n",p.lane_magnitude_ref[0],p.lane_attack_ref[0],p.broadband_magnitude_ref,p.broadband_attack_ref);
  memset(&r,0,sizeof(r)); r.schema_version=ODM_MUSIC_REACTION_SCHEMA_VERSION; r.tick_index=100; r.center_sample=48000;
  r.lane_q31[40]=UINT32_C(1000000000); r.lane_attack_q31[40]=UINT32_C(1200000000);
  r.family_q31[ODM_MUSIC_REACTION_FAMILY_BODY]=UINT32_C(500000000); r.broadband_q31=UINT32_C(400000000);
  memset(&base,0,sizeof(base));base.tick_index=r.tick_index;base.center_sample=r.center_sample;
  if(odm_composition_resolve_music_reaction(&base,&r,&c)!=ODM_STATUS_OK)return 5;
  for(i=0;i<ODM_COMPOSITION_RADIAL_SEGMENTS_MAX;i++) if(c.radial_q31[i]) printf("R,%u,%u\n",i,c.radial_q31[i]);
  if(odm_music_reaction_trace_radial(&r,20,&tr)!=ODM_STATUS_OK)return 6;
  printf("T,%u,%u,%u,%u\n",tr.radial_index,tr.lane_a,tr.lane_b,tr.direct_attack_q31);
  if(odm_music_reaction_trace_hires_radial(&r,40,&tr)!=ODM_STATUS_OK)return 8;
  printf("H,%u,%u,%u,%u\n",tr.radial_index,tr.lane_a,tr.lane_b,tr.direct_attack_q31);
  ev=r; ev.family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BASS]=UINT32_C(1200000000);
  ev.family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY]=UINT32_C(900000000);
  ev.family_attack_q31[ODM_MUSIC_REACTION_FAMILY_AIR]=UINT32_C(500000000);
  ev.broadband_attack_q31=UINT32_C(800000000);ev.event_threshold_q31=UINT32_C(600000000);
  ev.event_strength_q31=UINT32_C(966666667);ev.event_flags=ODM_MUSIC_REACTION_EVENT_ANY|ODM_MUSIC_REACTION_EVENT_BASS|ODM_MUSIC_REACTION_EVENT_BODY;
  ev.frame_flags=ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE;ev.macro_salience_q31=UINT32_C(1400000000);ev.macro_pulse_q31=UINT32_C(1500000000);
  if(odm_music_reaction_trace_event(&ev,&et)!=ODM_STATUS_OK)return 23;
  printf("Y,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",et.primary_family,et.primary_family_attack_q31,et.secondary_family,et.secondary_family_attack_q31,et.event_score_q31,et.broadband_attack_q31,et.event_strength_q31,et.macro_salience_q31,et.event_flags,et.frame_flags);
  base2=base;r2=r;base2.tick_index+=1000;base2.center_sample+=480000;r2.tick_index=base2.tick_index;r2.center_sample=base2.center_sample;
  if(odm_composition_resolve_music_reaction(&base2,&r2,&c2)!=ODM_STATUS_OK)return 7;
  printf("I,%u,%u,%u,%u,%u,%u,%u\n",c.radial_q31[40],c2.radial_q31[40],c.phase,c2.phase,c.ring_phase,c2.ring_phase,c.flags);

  memset(&rp,0,sizeof(rp));rp.schema_version=ODM_MUSIC_REACTION_SCHEMA_VERSION;
  rp.tick_count=2;rp.first_tick_index=0;rp.last_tick_index=1;
  for(i=0;i<ODM_MUSIC_REACTION_LANE_COUNT;i++)rp.lane_magnitude_ref[i]=UINT32_C(1000000000);
  for(i=0;i<ODM_MUSIC_REACTION_FAMILY_COUNT;i++)rp.family_magnitude_ref[i]=UINT32_C(1000000000);
  rp.broadband_magnitude_ref=UINT32_C(1000000000);
  init_tick(&rt0,0);init_tick(&rt1,1);
  rt0.lane_magnitude_q31[0]=UINT32_C(1000000000);rt0.lane_magnitude_q31[95]=UINT32_C(1000000000);
  rt0.family_magnitude_q31[ODM_MUSIC_REACTION_FAMILY_SUB]=UINT32_C(1000000000);
  rt0.family_magnitude_q31[ODM_MUSIC_REACTION_FAMILY_AIR]=UINT32_C(1000000000);
  rt0.broadband_magnitude_q31=UINT32_C(1000000000);
  if(odm_music_reaction_state_init(&rs)!=ODM_STATUS_OK)return 9;
  if(odm_music_reaction_resolve_tick(&rs,&rp,&rt0,&rf0)!=ODM_STATUS_OK)return 10;
  if(odm_music_reaction_resolve_tick(&rs,&rp,&rt1,&rf1)!=ODM_STATUS_OK)return 11;
  printf("D,%u,%u,%u,%u,%u\n",rf1.lane_q31[0],rf1.lane_q31[95],
         rf1.family_q31[ODM_MUSIC_REACTION_FAMILY_SUB],rf1.family_q31[ODM_MUSIC_REACTION_FAMILY_AIR],rf1.broadband_q31);

  memset(&hp,0,sizeof(hp)); hp.schema_version=ODM_MUSIC_REACTION_SCHEMA_VERSION;
  hp.tick_count=2;hp.first_tick_index=0;hp.last_tick_index=1;hp.broadband_attack_ref=UINT32_C(1000000);
  init_tick(&ht0,0);init_tick(&ht1,1);ht1.broadband_magnitude_q31=UINT32_C(1000000);
  if(odm_music_reaction_state_init(&hs)!=ODM_STATUS_OK)return 15;
  if(odm_music_reaction_resolve_tick(&hs,&hp,&ht0,&hf0)!=ODM_STATUS_OK)return 16;
  if(odm_music_reaction_resolve_tick(&hs,&hp,&ht1,&hf1)!=ODM_STATUS_OK)return 17;
  printf("N,%u,",hf1.broadband_attack_q31);
  init_tick(&ht0,0);init_tick(&ht1,1);ht1.broadband_magnitude_q31=(uint32_t)INT32_MAX;
  if(odm_music_reaction_state_init(&hs)!=ODM_STATUS_OK)return 18;
  if(odm_music_reaction_resolve_tick(&hs,&hp,&ht0,&hf0)!=ODM_STATUS_OK)return 19;
  if(odm_music_reaction_resolve_tick(&hs,&hp,&ht1,&hf1)!=ODM_STATUS_OK)return 20;
  printf("%u\n",hf1.broadband_attack_q31);

  for(i=0;i<20;i++){memset(&pf[i],0,sizeof(pf[i]));pf[i].schema_version=ODM_MUSIC_REACTION_SCHEMA_VERSION;
    pf[i].tick_index=i;pf[i].center_sample=(uint64_t)i*UINT64_C(480);pf[i].event_threshold_q31=UINT32_C(644245094);
    pf[i].lane_q31[3]=UINT32_C(1000)+i;
    if((i%4u)==1u)pf[i].lane_attack_q31[3]=UINT32_C(100000)+i;
    if(i==7u){pf[i].event_strength_q31=UINT32_C(700000);pf[i].event_flags=ODM_MUSIC_REACTION_EVENT_ANY|ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;}}
  {uint64_t pc=0,smp=0,last=UINT64_C(19)*UINT64_C(480);
   while(smp<=last){ps[pc++]=smp;smp+=UINT64_C(800);}
   if(ps[pc-1]!=last)ps[pc++]=last;
   if(odm_music_reaction_project_timeline(pf,20,ps,pc,pq,32)!=ODM_STATUS_OK)return 21;
   for(i=0;i<pc;i++)printf("Q,%u,%llu,%llu,%u,%llu,%llu,%u,%u,%u,%u,%u\n",i,
      (unsigned long long)pq[i].presentation_sample,(unsigned long long)pq[i].source_tick_index,
      pq[i].captured_tick_count,(unsigned long long)pq[i].first_captured_tick_index,
      (unsigned long long)pq[i].last_captured_tick_index,pq[i].lane_q31[3],pq[i].lane_attack_q31[3],
      pq[i].captured_event_tick_count,pq[i].event_strength_q31,pq[i].event_flags);}
  ps[0]=UINT64_C(2501);
  if(odm_music_reaction_project_timeline(pf,20,ps,1,&seekp,1)!=ODM_STATUS_OK)return 22;
  printf("S,%llu,%u,%u,%u\n",(unsigned long long)seekp.source_tick_index,seekp.captured_tick_count,seekp.lane_attack_q31[3],seekp.event_strength_q31);
  printf("V,%d\n",(int)odm_music_reaction_project_presentation(pf,20,-1,UINT64_C(800),&seekp));

  for(i=0;i<40;i++){memset(&ofin[i],0,sizeof(ofin[i]));ofin[i].schema_version=ODM_MUSIC_REACTION_SCHEMA_VERSION;
    ofin[i].tick_index=i;ofin[i].center_sample=(uint64_t)i*UINT64_C(480);ofin[i].event_threshold_q31=UINT32_C(644245094);}
  {static const uint32_t sv[5]={UINT32_C(858993459),UINT32_C(1288490188),UINT32_C(1932735282),UINT32_C(1503238553),UINT32_C(858993459)};
   for(i=0;i<5;i++){ofin[5+i].broadband_attack_q31=sv[i];ofin[5+i].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BASS]=sv[i];ofin[5+i].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY]=sv[i];}}
  ofin[24].broadband_attack_q31=UINT32_C(1073741824);ofin[25].broadband_attack_q31=UINT32_C(1932735282);ofin[25].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BASS]=UINT32_C(1932735282);ofin[25].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY]=UINT32_C(1932735282);ofin[26].broadband_attack_q31=UINT32_C(1073741824);
  if(odm_music_reaction_refine_events_offline(ofin,40,ofout,40)!=ODM_STATUS_OK)return 12;
  for(i=0;i<40;i++)if(ofout[i].event_strength_q31||ofout[i].event_flags)printf("O,%u,%u,%u,%u,%u,%u,%u\n",i,ofout[i].event_strength_q31,ofout[i].event_pulse_q31,ofout[i].event_flags,ofout[i].macro_salience_q31,ofout[i].macro_pulse_q31,ofout[i].frame_flags);
  return 0;
}
'''

def nearest_rank(values:list[int], percentile_num:int, percentile_den:int)->int:
    if not values: return 0
    vals=sorted(values)
    # Engine contract: ceil(N*p), converted to zero-based rank.
    rank=(len(vals)*percentile_num + percentile_den - 1)//percentile_den
    rank=max(1,min(len(vals),rank))
    return vals[rank-1]

def main()->int:
    ap=argparse.ArgumentParser(); ap.add_argument('--root',required=True); ap.add_argument('--cc',default='gcc'); ap.add_argument('--library',required=True); a=ap.parse_args()
    root=pathlib.Path(a.root).resolve(); lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-music-reaction-oracle-') as td0:
        td=pathlib.Path(td0); src=td/'probe.c'; exe=td/'probe'; src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        c=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if c.returncode: raise SystemExit('music reaction probe compile failed\n'+c.stdout+c.stderr)
        x=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
        if x.returncode: raise SystemExit(f'music reaction probe failed rc={x.returncode}\n{x.stdout}\n{x.stderr}')
    rows=[ln.split(',') for ln in x.stdout.splitlines() if ln.strip()]
    expected_policy=expected_policy_bytes()
    k=next(r for r in rows if r[0]=='K')
    got_policy=bytes.fromhex(k[1])
    if got_policy!=expected_policy:
        j=next(i for i,(u,v) in enumerate(zip(got_policy,expected_policy)) if u!=v)
        raise SystemExit(f'music-reaction policy byte mismatch offset={j} got={got_policy[j]} expected={expected_policy[j]}')
    jrow=next(r for r in rows if r[0]=='J')
    exp_hash=hashlib.sha256(expected_policy).hexdigest()
    if jrow[1]!=exp_hash: raise SystemExit(f'music-reaction policy hash mismatch got={jrow[1]} expected={exp_hash}')
    edges=[(int(r[1]),int(r[2]),int(r[3])) for r in rows if r[0]=='E']
    expected_edges=[(i,LANE_EDGES[i],LANE_EDGES[i+1]) for i in range(96)]
    if edges!=expected_edges: raise SystemExit('lane topology mismatch')
    xs=[(int(r[1]),int(r[2])) for r in rows if r[0]=='X']
    if xs != [(9,Q)]: raise SystemExit(f'single-bin locality mismatch: {xs}')
    p=next(r for r in rows if r[0]=='P'); got=tuple(map(int,p[1:]))
    values=[]
    for i in range(220):
        v=1000000
        if i%10==1:v=1100000
        if i==219:v=Q
        values.append(v)
    positive=[values[i]-values[i-1] for i in range(1,len(values)) if values[i]>values[i-1]]
    exp=(nearest_rank(values,99,100),nearest_rank(positive,95,100),nearest_rank(values,99,100),nearest_rank(positive,95,100))
    if got!=exp: raise SystemExit(f'robust profile mismatch got={got} expected={exp}')
    rad=[(int(r[1]),int(r[2])) for r in rows if r[0]=='R']
    if len(rad)!=1 or rad[0][0]!=40 or rad[0][1]<=0: raise SystemExit(f'radial locality mismatch: {rad}')
    t=next(r for r in rows if r[0]=='T'); trace=tuple(map(int,t[1:]))
    if trace!=(20,40,41,1200000000): raise SystemExit(f'legacy provenance mismatch: {trace}')
    h=next(r for r in rows if r[0]=='H'); hires=tuple(map(int,h[1:]))
    if hires!=(40,40,40,1200000000): raise SystemExit(f'hires provenance mismatch: {hires}')
    y=next(r for r in rows if r[0]=='Y'); event_trace=tuple(map(int,y[1:]))
    exp_event_trace=(1,1200000000,3,900000000,900000000,800000000,966666667,1400000000,64|2|8,1)
    if event_trace!=exp_event_trace: raise SystemExit(f'event provenance mismatch: {event_trace}')
    iso=next(r for r in rows if r[0]=='I'); a1,a2,phase1,phase2,ring1,ring2,flags=map(int,iso[1:])
    expected_flags=4|8|16|32
    if a1!=a2 or phase1!=0 or phase2!=0 or ring1!=0 or ring2!=0 or flags!=expected_flags:
        raise SystemExit(f'procedural isolation mismatch: radial={a1}/{a2} phase={phase1}/{phase2} ring={ring1}/{ring2} flags={flags}')
    d=next(r for r in rows if r[0]=='D'); release=tuple(map(int,d[1:]))
    exp_release=(Q-Q//12,Q-Q//4,Q-Q//12,Q-Q//4,Q-Q//8)
    if release!=exp_release:
        raise SystemExit(f'frequency-aware release mismatch got={release} expected={exp_release}')
    def attack_norm(v:int,ref:int)->int:
        pivot=1503238553
        if v==0 or ref==0:return 0
        if ref>=Q:return min(Q,(v*Q+ref//2)//ref)
        if v<=ref:return min(Q,(v*pivot+ref//2)//ref)
        inp=Q-ref; out=Q-pivot
        return min(Q,pivot+((v-ref)*out+inp//2)//inp)
    def shape(v:int)->int:
        if v<=0:return 0
        root=int((v<<31)**0.5)
        while (root+1)*(root+1) <= (v<<31): root+=1
        while root*root > (v<<31): root-=1
        root=min(root,Q)
        return min(Q,(3*v+root+2)//4)
    n=next(r for r in rows if r[0]=='N'); got_headroom=tuple(map(int,n[1:]))
    exp_headroom=(shape(attack_norm(1000000,1000000)),Q)
    if got_headroom!=exp_headroom:
        raise SystemExit(f'attack headroom mismatch got={got_headroom} expected={exp_headroom}')
    qrows=[tuple(map(int,r[1:])) for r in rows if r[0]=='Q']
    expected_q=[]
    samples=list(range(0,19*480+1,800))
    if samples[-1] != 19*480:samples.append(19*480)
    prev=-1
    consumed=[]
    for qi,pres in enumerate(samples):
        src=pres//480
        begin=0 if prev<0 else prev//480+1
        end=src
        captured=list(range(begin,end+1)) if begin<=end else []
        consumed.extend(captured)
        attack=max([100000+j if j%4==1 else 0 for j in captured],default=0)
        ev=[j for j in captured if j==7]
        first=captured[0] if captured else 0
        last=captured[-1] if captured else 0
        expected_q.append((qi,pres,src,len(captured),first,last,1000+src,attack,len(ev),700000 if ev else 0,(64|128) if ev else 0))
        prev=pres
    if qrows!=expected_q:
        raise SystemExit(f'causal projection mismatch\ngot={qrows}\nexpected={expected_q}')
    if consumed != list(range(20)):
        raise SystemExit(f'projection source consumption mismatch: {consumed}')
    seek=next(r for r in rows if r[0]=='S'); got_seek=tuple(map(int,seek[1:]))
    if got_seek!=(2501//480,0,0,0):
        raise SystemExit(f'projection seek replay mismatch: {got_seek}')
    invalid=next(r for r in rows if r[0]=='V')
    if int(invalid[1]) != 1:
        raise SystemExit(f'projection nonzero -1 predecessor should be invalid_argument, got {invalid[1]}')
    peaks=[tuple(map(int,r[1:])) for r in rows if r[0]=='O']
    if [x[0] for x in peaks] != [7,25]:
        raise SystemExit(f'offline peak selection mismatch: {peaks}')
    for tick,strength,pulse,flags,salience,macro_pulse,frame_flags in peaks:
        if strength<=0 or pulse<strength or (flags & 64)==0 or (flags & 128)==0:
            raise SystemExit(f'offline peak semantics mismatch at tick {tick}: {(strength,pulse,flags)}')
        if salience<=0 or macro_pulse<salience or frame_flags!=1:
            raise SystemExit(f'contextual salience semantics mismatch at tick {tick}: {(salience,macro_pulse,frame_flags)}')
    print('MUSIC REACTION ORACLE: PASS')
    print(f'  policy v11: {POLICY_BYTES} canonical bytes, sha256={exp_hash}')
    print('  96 spectral lanes: frozen contiguous topology, DC excluded, 23.4 Hz..18 kHz')
    print('  locality: FFT bin 10 -> lane 9 only; lane 40 -> high-resolution radial 40 only')
    print(f'  robust refs: magnitude P99={got[0]}, positive-attack P95={got[1]} despite INT32_MAX outlier')
    print('  provenance: legacy radial 20 -> lanes 40/41; hires radial 40 -> lane 40 exactly')
    print('  event provenance: broadband + strongest two canonical families + score/threshold/raw/context fields reconstructed exactly')
    print('  strict causal isolation: project-time changed; phase/ring stayed zero, flags include HIRES|STRICT_CAUSAL|RADIAL_PROVENANCE|RADIAL_TIMESCALE, radial amplitude byte-identical')
    print(f'  frequency-aware release: SUB={release[0]}, AIR={release[1]}, broadband={release[4]}')
    print(f'  attack headroom: P95 reference -> {got_headroom[0]}/{Q}; absolute Q1.31 max -> {got_headroom[1]}/{Q}')
    print('  causal projection: 20/20 source ticks consumed exactly once at 800-sample presentation cadence; seek replays no historical impulse')
    print('  offline onset refinement: candidate-median gate preserves local maxima at ticks 7 and 25; no beat/BPM claim')
    print('  contextual salience: full-track macro lane is separate from raw causal event evidence and carries explicit stream flags')
    return 0
if __name__=='__main__': raise SystemExit(main())
