#!/usr/bin/env python3
"""Independent Gate-4 Frame State oracle over every frame of a canonical score."""
from __future__ import annotations
import argparse, pathlib, subprocess, tempfile

INT32_MAX=(1<<31)-1; INT32_MIN=-(1<<31); Q32=1<<32; MASK64=(1<<64)-1

def q1_ratio(n:int,d:int)->int:
    assert d>0 and abs(n)<=d
    if abs(n)==d:return INT32_MIN if n<0 else INT32_MAX
    mag=abs(n); q=0; rem=mag
    for _ in range(31):
        q <<= 1
        if rem >= d-rem: q|=1; rem -= d-rem
        else: rem += rem
    if rem and rem >= d-rem:q+=1
    if n<0:return INT32_MIN if q==(1<<31) else -q
    return min(q,INT32_MAX)

def q1_mul(a:int,b:int)->int:
    p=a*b; m=abs(p); r=(m>>31)+(1 if (m&0x7fffffff)>=0x40000000 else 0)
    if p>=0:return min(r,INT32_MAX)
    return INT32_MIN if r >= (1<<31) else -r

def expected_frame(fi:int):
    sample=fi*1600; end=sample+1600
    if sample < 24000: a,b,tr,prog=1,0,0,0
    elif sample < 30000: a,b,tr,prog=1,2,1,q1_ratio(sample-24000,6000)
    else: a,b,tr,prog=2,0,0,0
    expected_tick=sample//480
    nodes=[]
    specs=[
      (1,1,1,0x10001,0,-100,0,30000,INT32_MAX),
      (2,1,2,0x10002,0,0,0,30000,INT32_MAX),
      (3,2,1,0x10001,0,-100,24000,48000,INT32_MAX),
      (4,2,2,0x10003,1,0,24000,48000,INT32_MAX),
      (5,2,5,0x30003,0,10,24000,48000,1<<30),
    ]
    for nid,sid,role,cap,res,zidx,start,stop,baseop in specs:
        if sid not in (a,b) or not(start<=sample<stop):continue
        weight=INT32_MAX
        if b:
            weight=INT32_MAX-prog if sid==a else prog
        x=0
        if nid==5:
            # Linear x automation across [24000,48000).
            x=q1_ratio(sample-24000,24000)<<1
            op=INT32_MAX if sample < 30000 else baseop
        else: op=baseop
        final = op if weight==INT32_MAX else (weight if op==INT32_MAX else q1_mul(op,weight))
        seed=(nid*0x9e3779b97f4a7c15)&MASK64
        nodes.append((nid,sid,role,cap,res,zidx,x,0,Q32,Q32,final,0,seed,weight,0,1 if nid==5 else 0,0,0,0,0,INT32_MAX,INT32_MAX))
    transfers=[]
    if 24000>=sample and 24000<end: transfers.append((1,1,2,1,2))
    state=(1,30,fi,sample,a,b,tr,prog,expected_tick,len(nodes),len(transfers),0)
    return state,nodes,transfers

PROBE=r'''#include "odm_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void node(odm_score_node*n,unsigned id,unsigned scene,unsigned role,unsigned cap,unsigned resource,long long start,long long end,int z){memset(n,0,sizeof(*n));n->node_id=id;n->scene_id=scene;n->role=role;n->capability_id=cap;n->capability_major=1;n->resource_id=resource;n->state_model=ODM_STATELESS;n->z_index=z;n->start_sample=start;n->end_sample=end;n->scale_x_micro=ODM_MICRO_ONE;n->scale_y_micro=ODM_MICRO_ONE;n->opacity_micro=ODM_MICRO_ONE;n->seed_domain=(uint64_t)id*UINT64_C(0x9e3779b97f4a7c15);}
int main(void){odm_score_header h={0};odm_score_resource r[1]={{0}};odm_score_act a[1]={{0}};odm_score_scene s[2]={{0}};odm_score_node n[5];odm_score_cue q[1]={{0}};odm_score_modulator m[1]={{0}};odm_score_automation au[1]={{0}};odm_score_transition tr[1]={{0}};odm_score_state_transfer tf[1]={{0}};odm_score_view v={0};odm_music_map_binding music={0};uint64_t need=0,nr=0,tt=0;uint8_t*b;int fi;
h.schema_version_major=1;h.schema_version_minor=1;h.fps=30;h.width=1920;h.height=1080;h.project_end_sample=48000;h.project_seed=UINT64_C(0x123456789abcdef0);h.resource_count=1;h.act_count=1;h.scene_count=2;h.node_count=5;h.cue_count=1;h.modulator_count=1;h.automation_count=1;h.transition_count=1;h.transfer_count=1;r[0].resource_id=1;r[0].kind=ODM_SCORE_RESOURCE_IMAGE;if(odm_sha256("image-one",9,&r[0].content_sha256))return 2;a[0].act_id=1;a[0].end_sample=48000;s[0].scene_id=1;s[0].act_id=1;s[0].environment_node_id=1;s[0].primary_node_id=2;s[0].end_sample=30000;s[1].scene_id=2;s[1].act_id=1;s[1].environment_node_id=3;s[1].primary_node_id=4;s[1].start_sample=24000;s[1].end_sample=48000;node(&n[0],1,1,1,0x10001,0,0,30000,-100);node(&n[1],2,1,2,0x10002,0,0,30000,0);node(&n[2],3,2,1,0x10001,0,24000,48000,-100);node(&n[3],4,2,2,0x10003,1,24000,48000,0);node(&n[4],5,2,5,0x30003,0,24000,48000,10);n[4].state_model=ODM_EVENT_HISTORY;n[4].state_slot=1;n[4].opacity_micro=500000;q[0].cue_id=1;q[0].scene_id=1;q[0].kind=1;q[0].sample=1000;m[0].modulator_id=1;m[0].node_id=5;m[0].target_parameter=5;m[0].source=3;m[0].scale_micro=64000000;au[0].automation_id=1;au[0].node_id=5;au[0].target_parameter=1;au[0].interpolation=2;au[0].start_sample=24000;au[0].end_sample=48000;au[0].to_micro=1000000;tr[0].transition_id=1;tr[0].from_scene_id=1;tr[0].to_scene_id=2;tr[0].kind=1;tr[0].start_sample=24000;tr[0].end_sample=30000;tf[0].transfer_id=1;tf[0].from_scene_id=1;tf[0].to_scene_id=2;tf[0].state_slot=1;tf[0].mode=2;tf[0].at_sample=24000;v.header=&h;v.resources=r;v.acts=a;v.scenes=s;v.nodes=n;v.cues=q;v.modulators=m;v.automation=au;v.transitions=tr;v.transfers=tf;if(odm_sha256("music-map",9,&music.music_map_sha256)||odm_music_policy_current_sha256(&music.music_policy_sha256))return 3;music.canonical_frames=30000;if(odm_score_compile_render_ir(&v,&music,NULL,0,&need,NULL)!=ODM_STATUS_BUFFER_TOO_SMALL)return 4;b=malloc((size_t)need);if(!b)return 5;if(odm_score_compile_render_ir(&v,&music,b,need,&need,NULL))return 6;
for(fi=0;fi<30;fi++){odm_music_analysis_tick tick={0};const odm_music_analysis_tick*tp=NULL;odm_frame_node_state ns[5];odm_frame_transfer_event ts[2];odm_frame_state fs;int64_t sample=(int64_t)fi*1600;unsigned j;if(sample>=24000&&sample<30000){tick.tick_index=(uint64_t)sample/480u;tick.center_sample=tick.tick_index*480u;tick.rms_mid_q31=INT32_MAX/2;tp=&tick;}if(odm_frame_state_resolve(b,need,fi,tp,ns,5,ts,2,&nr,&tt,&fs))return 10+fi;printf("F,%u,%d,%lld,%lld,%u,%u,%u,%d,%llu,%u,%u,%u\n",fs.schema_version,fs.fps,(long long)fs.frame_index,(long long)fs.sample,fs.scene_a_id,fs.scene_b_id,fs.transition_id,fs.transition_progress_q31,(unsigned long long)fs.expected_music_tick,fs.node_count,fs.transfer_count,fs.flags);for(j=0;j<fs.node_count;j++){odm_frame_node_state*x=&ns[j];printf("N,%u,%u,%u,%u,%u,%d,%lld,%lld,%lld,%lld,%d,%u,%llu,%d,%u,%u,%u,%u,%u,%u,%d,%d\n",x->node_id,x->scene_id,x->role,x->capability_id,x->resource_id,x->z_index,(long long)x->x_q32_32,(long long)x->y_q32_32,(long long)x->scale_x_q32_32,(long long)x->scale_y_q32_32,x->opacity_q31,x->phase,(unsigned long long)x->seed_domain,x->scene_weight_q31,x->flags,x->state_slot,x->entry_kind,x->exit_kind,x->entry_duration_samples,x->exit_duration_samples,x->entry_weight_q31,x->exit_weight_q31);}for(j=0;j<fs.transfer_count;j++){odm_frame_transfer_event*x=&ts[j];printf("T,%u,%u,%u,%u,%u\n",x->transfer_id,x->from_scene_id,x->to_scene_id,x->state_slot,x->mode);}}
free(b);return 0;}
'''

def parse_line(line):
    a=line.strip().split(','); return a[0],tuple(int(x) for x in a[1:])

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);args=ap.parse_args();root=pathlib.Path(args.root).resolve();lib=pathlib.Path(args.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm_frame_oracle_') as td:
        td=pathlib.Path(td);c=td/'p.c';e=td/'p';c.write_text(PROBE)
        subprocess.run([args.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(c),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(e)],check=True,cwd=root)
        run=subprocess.run([str(e)],check=True,cwd=root,text=True,capture_output=True)
    lines=[parse_line(x) for x in run.stdout.splitlines() if x.strip()]
    pos=0; node_total=0; transfer_total=0
    for fi in range(30):
        state,nodes,transfers=expected_frame(fi)
        if pos>=len(lines) or lines[pos] != ('F',state): raise SystemExit(f'frame state mismatch frame={fi}: got={lines[pos] if pos<len(lines) else None} expected={state}')
        pos+=1
        for exp in nodes:
            if pos>=len(lines) or lines[pos] != ('N',exp): raise SystemExit(f'node state mismatch frame={fi}: got={lines[pos] if pos<len(lines) else None} expected={exp}')
            pos+=1; node_total+=1
        for exp in transfers:
            if pos>=len(lines) or lines[pos] != ('T',exp): raise SystemExit(f'transfer mismatch frame={fi}: got={lines[pos] if pos<len(lines) else None} expected={exp}')
            pos+=1; transfer_total+=1
    if pos != len(lines): raise SystemExit(f'extra probe lines: {len(lines)-pos}')
    print(f'Frame State oracle: pass (30 frames, {node_total} node states, {transfer_total} transfer events)')

if __name__=='__main__':main()
