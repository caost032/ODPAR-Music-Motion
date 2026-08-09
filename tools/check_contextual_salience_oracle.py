#!/usr/bin/env python3
"""Independent contextual-salience oracle for ODPAR Music.

Reconstructs local-peak eligibility, candidate-median macro gate, refractory
publication, exact nearest-rank salience knots and the piecewise Q1.31 mapping.
It also perturbs a high-strength candidate that is guaranteed to be suppressed
by refractory and requires published salience to remain byte-identical.
"""
from __future__ import annotations
import argparse, pathlib, subprocess, tempfile

Q = 2147483647
THRESH = (Q * 30 + 50) // 100
RADIUS = 2
REFRACTORY = 14
QUANTILES = [10,20,30,40,50,60,70,80,90,99]

PROBE = r'''#include "odm_music_reaction.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define N 440u
static uint32_t qpct(uint32_t p) {
    return (uint32_t)(((uint64_t)(uint32_t)INT32_MAX * p + 50u) / 100u);
}
static void init_seq(odm_music_reaction_frame *f, uint32_t suppressed_pct) {
    uint32_t i;
    memset(f,0,sizeof(*f)*N);
    for(i=0;i<N;i++) {
        f[i].schema_version=ODM_MUSIC_REACTION_SCHEMA_VERSION;
        f[i].tick_index=i; f[i].center_sample=(uint64_t)i*UINT64_C(480);
        f[i].event_threshold_q31=qpct(30u);
    }
    for(i=0;i<21u;i++) {
        uint32_t tick=20u+i*20u;
        uint32_t v=qpct(35u+i*2u);
        f[tick].broadband_attack_q31=v;
        f[tick].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BASS]=v;
        f[tick].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY]=v;
    }
    /* Local peak, but 5 ticks after the first macro-published base event.
     * It is therefore rejected by the 14-tick refractory. */
    f[225].broadband_attack_q31=qpct(suppressed_pct);
    f[225].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BASS]=qpct(suppressed_pct);
    f[225].family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BODY]=qpct(suppressed_pct);
}
static int run(uint32_t tag, uint32_t suppressed_pct) {
    odm_music_reaction_frame in[N], out[N]; uint32_t i;
    init_seq(in,suppressed_pct);
    if(odm_music_reaction_refine_events_offline(in,N,out,N)!=ODM_STATUS_OK)return 2;
    for(i=0;i<N;i++) if((out[i].event_flags & ODM_MUSIC_REACTION_EVENT_ANY)!=0u)
        printf("E,%u,%u,%u,%u,%u,%u\n",tag,i,out[i].event_strength_q31,
               out[i].macro_salience_q31,out[i].macro_pulse_q31,out[i].frame_flags);
    printf("S,%u,%u,%u,%u\n",tag,out[225].event_strength_q31,
           out[225].macro_salience_q31,out[225].event_flags);
    return 0;
}
int main(void){ if(run(0u,90u)!=0)return 2; if(run(1u,100u)!=0)return 3; return 0; }
'''

def qpct(p:int)->int: return (Q*p+50)//100

def score(fr:dict)->int:
    fam=sorted(fr.get('families',[0,0,0,0,0,0]), reverse=True)
    return max(fr.get('broad',0), fam[1])

def strength(fr:dict)->int:
    fam=sorted(fr.get('families',[0,0,0,0,0,0]), reverse=True)
    return min(Q,(fr.get('broad',0)+fam[0]+fam[1]+1)//3)

def nearest(vals:list[int], num:int, den:int=100)->int:
    vals=sorted(vals)
    rank=(len(vals)*num+den-1)//den
    rank=max(1,min(len(vals),rank))
    return vals[rank-1]

def expected(suppressed_pct:int):
    frames=[{'broad':0,'families':[0]*6,'thr':THRESH} for _ in range(440)]
    for i in range(21):
        t=20+i*20; v=qpct(35+i*2)
        frames[t]['broad']=v; frames[t]['families'][1]=v; frames[t]['families'][3]=v
    v=qpct(suppressed_pct); frames[225]['broad']=v; frames[225]['families'][1]=v; frames[225]['families'][3]=v
    def local(i:int)->bool:
        sc=score(frames[i])
        if sc<=frames[i]['thr']: return False
        lo=max(0,i-RADIUS); hi=min(len(frames)-1,i+RADIUS)
        for j in range(lo,hi+1):
            if j==i: continue
            other=score(frames[j])
            if other>sc or (other==sc and j<i): return False
        return True
    candidates=[i for i in range(len(frames)) if local(i)]
    gate=nearest([score(frames[i]) for i in candidates],50)
    published=[]; nxt=0
    for i in range(len(frames)):
        if local(i) and score(frames[i])>=gate and i>=nxt:
            published.append(i); nxt=i+REFRACTORY
    vals=[strength(frames[i]) for i in published]
    knots=[nearest(vals,q) for q in QUANTILES]
    def sal(v:int)->int:
        if v==0:return 0
        floor=qpct(10)
        if v<=knots[0]: return floor
        prev_ref=knots[0]; prev_out=floor
        for idx in range(1,len(knots)):
            hi_ref=knots[idx]
            hi_out=Q if idx+1==len(knots) else (Q*(idx+1)+5)//10
            if v<=hi_ref:
                if hi_ref<=prev_ref:return hi_out
                ir=hi_ref-prev_ref; orng=hi_out-prev_out
                return min(Q,prev_out+((v-prev_ref)*orng+ir//2)//ir)
            prev_ref=hi_ref; prev_out=hi_out
        return Q
    return [(i,strength(frames[i]),sal(strength(frames[i]))) for i in published], gate, knots

def main()->int:
    ap=argparse.ArgumentParser(); ap.add_argument('--root',required=True); ap.add_argument('--cc',default='cc'); ap.add_argument('--library',required=True); a=ap.parse_args()
    root=pathlib.Path(a.root).resolve(); lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-context-salience-oracle-') as td0:
        td=pathlib.Path(td0); src=td/'probe.c'; exe=td/'probe'; src.write_text(PROBE+'\n')
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        c=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if c.returncode: raise SystemExit('contextual salience probe compile failed\n'+c.stdout+c.stderr)
        x=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
        if x.returncode: raise SystemExit(f'contextual salience probe failed rc={x.returncode}\n{x.stdout}\n{x.stderr}')
    rows=[r.split(',') for r in x.stdout.splitlines() if r.strip()]
    streams={0:[],1:[]}; suppressed={}
    for r in rows:
        if r[0]=='E':
            tag,tick,raw,sal,pulse,flags=map(int,r[1:]); streams[tag].append((tick,raw,sal,pulse,flags))
        elif r[0]=='S':
            tag,raw,sal,flags=map(int,r[1:]); suppressed[tag]=(raw,sal,flags)
    for tag,pct in ((0,90),(1,100)):
        exp,gate,knots=expected(pct)
        got=[(t,raw,sal) for t,raw,sal,_,_ in streams[tag]]
        if got!=exp: raise SystemExit(f'contextual salience mismatch tag={tag}\ngot={got}\nexpected={exp}\ngate={gate}\nknots={knots}')
        for t,raw,sal,pulse,flags in streams[tag]:
            if flags!=1 or pulse<sal: raise SystemExit(f'bad contextual fields tag={tag} tick={t}: {(raw,sal,pulse,flags)}')
        if suppressed.get(tag)!=(0,0,0): raise SystemExit(f'refractory-suppressed candidate leaked tag={tag}: {suppressed.get(tag)}')
    # A suppressed candidate is not allowed to recalibrate visible salience.
    canon0=[(t,raw,sal) for t,raw,sal,_,_ in streams[0]]
    canon1=[(t,raw,sal) for t,raw,sal,_,_ in streams[1]]
    if canon0!=canon1: raise SystemExit('suppressed outlier changed published contextual salience')
    vals=[x[2] for x in canon0]
    if min(vals)!=qpct(10) or max(vals)!=Q or len(set(vals))<8:
        raise SystemExit(f'insufficient salience dynamic range: min={min(vals)} max={max(vals)} distinct={len(set(vals))}')
    print('CONTEXTUAL SALIENCE ORACLE: PASS')
    print(f'  published events={len(canon0)}, salience distinct={len(set(vals))}, range={min(vals)}..{max(vals)} Q1.31')
    print('  exact reconstruction: local peaks -> candidate median gate -> refractory publication -> P10..P90/P99 knots -> piecewise salience')
    print('  negative control: suppressed 0.90 vs 1.00 outlier has zero event fields and cannot recalibrate published salience')
    return 0
if __name__=='__main__': raise SystemExit(main())
