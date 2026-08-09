#!/usr/bin/env python3
"""Independent Gate-7 Visual Memory and procedural-frame oracle.

The C probe only exercises public ODPAR APIs to build a canonical three-scene
Score, resolve two Visual Memory snapshots and render frames 20/21. Python then
rebuilds COPY/RESET ancestry, SplitMix-domain seeds, latest-32 eviction,
Entry/Exit Q1.31, Grid/Halo/Residual/Particles and final linear RGBA16LE pixels
without calling engine math/serialization helpers for expected values.
"""
from __future__ import annotations
import argparse, hashlib, pathlib, struct, subprocess, tempfile

MASK64=(1<<64)-1
Q31=(1<<31)-1
Q27=1<<27
VIS=0x56495355414C4556
PROJECT=0x4F44504152473731
NODE_DOMAIN=0x100000001B3
Q31_010=214748365; Q31_018=386547057; Q31_025=536870912
Q31_035=751619277; Q31_055=1181116006; Q31_070=1503238553; Q31_090=1932735282

def splitmix(v:int)->int:
    v &= MASK64; v ^= v>>30; v=(v*0xbf58476d1ce4e5b9)&MASK64
    v ^= v>>27; v=(v*0x94d049bb133111eb)&MASK64; v ^= v>>31
    return v&MASK64

def derive(root:int,domain:int,entity:int)->int:
    d=splitmix((domain+0x9e3779b97f4a7c15)&MASK64)
    e=splitmix((entity+0xd1b54a32d192ed03)&MASK64)
    return splitmix((root^d^e)&MASK64)

def q1_ratio(n:int,d:int)->int:
    assert d>0 and abs(n)<=d
    if abs(n)==d:return -1<<31 if n<0 else Q31
    mag=abs(n);q=0;rem=mag
    for _ in range(31):
        q<<=1
        if rem>=d-rem:q|=1;rem-=d-rem
        else:rem+=rem
    if rem and rem>=d-rem:q+=1
    if n<0:return -(q) if q<(1<<31) else -(1<<31)
    return min(q,Q31)

def qmul(a:int,b:int)->int:
    if a==0 or b==0:return 0
    if a==Q31:return b
    if b==Q31:return a
    p=a*b;m=abs(p);r=(m>>31)+(1 if (m&0x7fffffff)>=0x40000000 else 0)
    return min(r,Q31) if p>=0 else max(-r,-(1<<31))

def div_away(n:int,d:int)->int:
    # C truncation toward zero + ties away from zero.
    q=abs(n)//abs(d)
    if (n<0) != (d<0): q=-q
    r=n-q*d; rm=abs(r);dm=abs(d)
    if rm >= dm-rm: q += -1 if ((n<0)!=(d<0)) else 1
    return q

def q27_mul_q31(v:int,f:int)->int:return div_away(v*f,Q31)
def q27_u16(v:int)->int:return max(0,min(65535,div_away(max(0,min(Q27,v))*65535,Q27)))
def q31_u16(v:int)->int:return max(0,min(65535,div_away(max(0,min(Q31,v))*65535,Q31)))

def blend(dst:tuple[int,int,int,int],src:tuple[int,int,int,int])->tuple[int,int,int,int]:
    dr,dg,db,da=dst;sr,sg,sb,sa=src;inv=Q31-sa
    r=min(Q27,sr+q27_mul_q31(dr,inv));g=min(Q27,sg+q27_mul_q31(dg,inv));b=min(Q27,sb+q27_mul_q31(db,inv))
    a=sa+((da*inv+Q31//2)//Q31);a=min(Q31,a)
    return r,g,b,a

def premul(r:int,g:int,b:int,a:int)->tuple[int,int,int,int]:return qmul(r,a)//16,qmul(g,a)//16,qmul(b,a)//16,a

def event_root(node_id:int,slot:int)->int:
    seed_domain=(NODE_DOMAIN*node_id)&MASK64
    return derive(PROJECT^seed_domain,VIS,slot)

def event_seed(root:int,slot:int,cue_id:int,sample:int)->int:
    entity=((cue_id<<32)^sample)&MASK64
    return derive(root,VIS^slot,entity)

def history(node_id:int,scene:int,slot:int,sample:int):
    # canonical score: scene2 COPYs slot7 from scene1 at 16000; scene3 RESETs at 32000.
    chains={1:[(1,sample)],2:[(2,sample),(1,16000)],3:[(3,sample)]}
    cues=[]
    cid=1
    for i in range(20):cues.append((cid,1,1,1000+i*700));cid+=1
    for i in range(20):cues.append((cid,2,2,17000+i*700));cid+=1
    for i in range(5):cues.append((cid,3,3,33000+i*1500));cid+=1
    root=event_root(node_id,slot); out=[]
    limits=dict(chains[scene])
    for cue_id,cs,kind,when in cues:
        if cs in limits and when<=limits[cs] and when<=sample:
            out.append((cue_id,kind,cs,when,sample-when,event_seed(root,slot,cue_id,when)))
            if len(out)>32:out.pop(0)
    return root,out

def grid(x,y,opacity,phase=0):
    spacing=24+((phase>>28)&15);off=(phase>>20)%spacing
    a=qmul(opacity,Q31_025) if ((x+off)%spacing<2 or (y+off)%spacing<2) else 0
    return premul(Q31_010,Q31_035,Q31_055,a)

def halo(x,y,w,h,opacity,phase=0):
    cx=w//2;cy=h//2;base=min(w,h)//5;span=min(w,h)//10
    radius=base+(((phase>>16)*span)>>16);thick=min(w,h)//64+1
    dx=x-cx;dy=y-cy;d2=dx*dx+dy*dy;lo=max(0,radius-thick)**2;hi=(radius+thick)**2
    a=qmul(opacity,Q31_055) if lo<=d2<=hi else 0
    return premul(Q31_018,Q31_055,Q31_090,a)

def residual(x,y,w,h,opacity,events):
    alpha=0
    for cue_id,kind,scene,sample,age_samples,seed in events:
        age=age_samples//480;life=240
        if age>=life:continue
        cx=seed%w;cy=(seed>>32)%h;radius=3+age//12
        if (x-cx)**2+(y-cy)**2<=radius*radius:
            decay=((life-age)*Q31)//life;a=qmul(opacity,decay);a=qmul(a,Q31_018);alpha=min(Q31,alpha+a)
    return premul(Q31_010,Q31_035,Q31_070,alpha)

def trunc0(n:int,d:int)->int:return (abs(n)//d)*(-1 if n<0 else 1)
def particle(x,y,w,h,opacity,events):
    alpha=0
    for cue_id,kind,scene,sample,age_samples,seed in events:
        age=age_samples//480;life=180
        if age>=life:continue
        for j in range(4):
            s=(seed^(0x9e3779b97f4a7c15*(j+1)))&MASK64
            px=s%w;py=(s>>32)%h;vx=((s>>16)&7)-3;vy=((s>>24)&7)-4
            px=(px+trunc0(vx*age,4))%w;py=(py+trunc0(vy*age,4))%h
            if -1<=px-x<=1 and -1<=py-y<=1:
                decay=((life-age)*Q31)//life;a=qmul(opacity,decay);a=qmul(a,Q31_035);alpha=min(Q31,alpha+a)
    return premul(Q31_055,Q31_070,Q31_090,alpha)

def expected_frame(fi:int)->bytes:
    sample=fi*1600; assert fi in (20,21)
    entry=0 if sample==32000 else q1_ratio(sample-32000,4800)
    _,ev11=history(11,3,7,sample);_,ev12=history(12,3,7,sample)
    out=bytearray()
    for y in range(32):
        for x in range(32):
            dst=(0,0,0,Q31) # opaque black void
            for src in (grid(x,y,Q31),halo(x,y,32,32,Q31),residual(x,y,32,32,entry,ev11),particle(x,y,32,32,Q31,ev12)):
                dst=blend(dst,src)
            for v in (q27_u16(dst[0]),q27_u16(dst[1]),q27_u16(dst[2]),q31_u16(dst[3])):out+=struct.pack('<H',v)
    return bytes(out)

PROBE=r'''#include "odm_renderer.h"
#include "odm_visual.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void node(odm_score_node*n,unsigned id,unsigned scene,unsigned role,unsigned cap,unsigned state,unsigned slot,long long start,long long end,int z){memset(n,0,sizeof(*n));n->node_id=id;n->scene_id=scene;n->role=role;n->capability_id=cap;n->capability_major=1;n->state_model=state;n->state_slot=slot;n->z_index=z;n->start_sample=start;n->end_sample=end;n->scale_x_micro=ODM_MICRO_ONE;n->scale_y_micro=ODM_MICRO_ONE;n->opacity_micro=ODM_MICRO_ONE;n->seed_domain=UINT64_C(0x100000001b3)*(uint64_t)id;}
static void ph(const odm_visual_history*h){unsigned i;printf("H,%u,%u,%u,%u,%lld,%llu\n",h->node_id,h->scene_id,h->state_slot,h->event_count,(long long)h->sample,(unsigned long long)h->root_seed);for(i=0;i<h->event_count;i++){const odm_visual_event*e=&h->events[i];printf("E,%u,%u,%u,%lld,%lld,%llu\n",e->cue_id,e->cue_kind,e->scene_id,(long long)e->sample,(long long)e->age_samples,(unsigned long long)e->seed);}}
int main(int argc,char**argv){odm_score_header h={0};odm_score_act act={0};odm_score_scene s[3]={{0}};odm_score_node n[12];odm_score_cue q[45]={{0}};odm_score_state_transfer t[2]={{0}};odm_score_view v={0};odm_music_map_binding m={0};uint64_t need=0,fb=0,sb=0,rf=0,rs=0;uint8_t*ir,*frame;void*scratch;odm_render_asset_set assets={0};odm_reference_frame_info info;odm_visual_history vh;unsigned i,k=0;FILE*f;if(argc!=3)return 2;
h.schema_version_major=1;h.schema_version_minor=1;h.fps=30;h.width=32;h.height=32;h.project_end_sample=48000;h.project_seed=UINT64_C(0x4f44504152473731);h.act_count=1;h.scene_count=3;h.node_count=12;h.cue_count=45;h.transfer_count=2;act.act_id=1;act.end_sample=48000;
s[0].scene_id=1;s[0].act_id=1;s[0].environment_node_id=1;s[0].primary_node_id=2;s[0].end_sample=16000;s[1].scene_id=2;s[1].act_id=1;s[1].environment_node_id=4;s[1].primary_node_id=5;s[1].start_sample=16000;s[1].end_sample=32000;s[2].scene_id=3;s[2].act_id=1;s[2].environment_node_id=7;s[2].primary_node_id=8;s[2].start_sample=32000;s[2].end_sample=48000;
node(&n[0],1,1,1,ODM_CAP_BLACK_VOID,0,0,0,16000,-100);node(&n[1],2,1,2,ODM_CAP_PRIMARY_EMPTY,0,0,0,16000,0);node(&n[2],3,1,5,ODM_CAP_RESIDUAL_EVENT_FIELD,ODM_EVENT_HISTORY,7,0,16000,10);node(&n[3],4,2,1,ODM_CAP_BLACK_VOID,0,0,16000,32000,-100);node(&n[4],5,2,2,ODM_CAP_PRIMARY_EMPTY,0,0,16000,32000,0);node(&n[5],6,2,5,ODM_CAP_RESIDUAL_EVENT_FIELD,ODM_EVENT_HISTORY,7,16000,32000,10);node(&n[6],7,3,1,ODM_CAP_BLACK_VOID,0,0,32000,48000,-100);node(&n[7],8,3,2,ODM_CAP_PRIMARY_EMPTY,0,0,32000,48000,0);node(&n[8],9,3,5,ODM_CAP_GRID_PROOF,0,0,32000,48000,10);node(&n[9],10,3,5,ODM_CAP_HALO,0,0,32000,48000,20);node(&n[10],11,3,5,ODM_CAP_RESIDUAL_EVENT_FIELD,ODM_EVENT_HISTORY,7,32000,48000,30);node(&n[11],12,3,6,ODM_CAP_DETERMINISTIC_PARTICLES,ODM_EVENT_HISTORY,7,32000,48000,40);n[10].entry_kind=ODM_ENTRY_FADE_SCALE;n[10].entry_duration_samples=4800;n[10].exit_kind=ODM_EXIT_FADE_SCALE;n[10].exit_duration_samples=4800;
for(i=0;i<20;i++){q[k].cue_id=k+1;q[k].scene_id=1;q[k].kind=1;q[k].sample=1000+(long long)i*700;k++;}for(i=0;i<20;i++){q[k].cue_id=k+1;q[k].scene_id=2;q[k].kind=2;q[k].sample=17000+(long long)i*700;k++;}for(i=0;i<5;i++){q[k].cue_id=k+1;q[k].scene_id=3;q[k].kind=3;q[k].sample=33000+(long long)i*1500;k++;}
t[0].transfer_id=1;t[0].from_scene_id=1;t[0].to_scene_id=2;t[0].state_slot=7;t[0].mode=ODM_TRANSFER_COPY;t[0].at_sample=16000;t[1].transfer_id=2;t[1].from_scene_id=2;t[1].to_scene_id=3;t[1].state_slot=7;t[1].mode=ODM_TRANSFER_RESET;t[1].at_sample=32000;
v.header=&h;v.acts=&act;v.scenes=s;v.nodes=n;v.cues=q;v.transfers=t;if(odm_sha256("visual-map",10,&m.music_map_sha256)||odm_music_policy_current_sha256(&m.music_policy_sha256))return 3;m.canonical_frames=48000;if(odm_score_compile_render_ir(&v,&m,NULL,0,&need,NULL)!=ODM_STATUS_BUFFER_TOO_SMALL)return 4;ir=malloc((size_t)need);if(!ir)return 5;if(odm_score_compile_render_ir(&v,&m,ir,need,&need,NULL))return 6;
if (odm_visual_history_resolve(ir,need,6,31000,&vh)) return 7;
ph(&vh);
if (odm_visual_history_resolve(ir,need,11,40000,&vh)) return 8;
ph(&vh);
if (odm_reference_render_requirements(ir,need,20,NULL,&fb,&sb)) return 9;
frame=malloc((size_t)fb);
scratch=malloc((size_t)sb);
if (!frame||!scratch) return 10;
if (odm_reference_render_frame(ir,need,20,NULL,&assets,NULL,scratch,sb,frame,fb,&rf,&rs,&info)) return 11;
f=fopen(argv[1],"wb");
if (!f) return 12;
fwrite(frame,1,(size_t)fb,f);
fclose(f);
printf("F,20,%lld,%llu\n",(long long)info.sample,(unsigned long long)info.pixel_bytes);
if (odm_reference_render_frame(ir,need,21,NULL,&assets,NULL,scratch,sb,frame,fb,&rf,&rs,&info)) return 13;
f=fopen(argv[2],"wb");
if (!f) return 14;
fwrite(frame,1,(size_t)fb,f);
fclose(f);
printf("F,21,%lld,%llu\n",(long long)info.sample,(unsigned long long)info.pixel_bytes);
free(scratch);
free(frame);
free(ir);
return 0;
}
'''

def parse_hist(lines):
    pos=0;hist=[]
    while pos<len(lines) and lines[pos].startswith('H,'):
        h=tuple(int(x) for x in lines[pos].split(',')[1:]);pos+=1;ev=[]
        for _ in range(h[3]):ev.append(tuple(int(x) for x in lines[pos].split(',')[1:]));pos+=1
        hist.append((h,ev))
    return hist,lines[pos:]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args();root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm_visual_oracle_') as td:
        td=pathlib.Path(td);c=td/'p.c';e=td/'p';f20=td/'f20.bin';f21=td/'f21.bin';c.write_text(PROBE+'\n')
        subprocess.run([a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-I',str(root/'include'),str(c),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(e)],check=True,cwd=root)
        r=subprocess.run([str(e),str(f20),str(f21)],check=True,cwd=root,text=True,capture_output=True);got20=f20.read_bytes();got21=f21.read_bytes()
    lines=[x for x in r.stdout.splitlines() if x.strip()];hs,rest=parse_hist(lines)
    exp_h=[]
    for node,scene,sample in ((6,2,31000),(11,3,40000)):
        rootseed,ev=history(node,scene,7,sample);exp_h.append(((node,scene,7,len(ev),sample,rootseed),ev))
    if hs!=exp_h:raise SystemExit(f'Visual history oracle mismatch: got={hs!r} expected={exp_h!r}')
    exp20=expected_frame(20);exp21=expected_frame(21)
    if got20!=exp20:
        i=next((i for i,(x,y) in enumerate(zip(got20,exp20)) if x!=y),min(len(got20),len(exp20)));raise SystemExit(f'Visual frame20 mismatch byte={i} got={got20[i:i+16].hex()} exp={exp20[i:i+16].hex()}')
    if got21!=exp21:
        i=next((i for i,(x,y) in enumerate(zip(got21,exp21)) if x!=y),min(len(got21),len(exp21)));raise SystemExit(f'Visual frame21 mismatch byte={i} got={got21[i:i+16].hex()} exp={exp21[i:i+16].hex()}')
    print(f'Visual oracle: pass ({sum(len(e) for _,e in hs)} event records, 2 frames, 2048 pixels / 8192 channels exact)')
    print('Frame 20 SHA-256:',hashlib.sha256(got20).hexdigest());print('Frame 21 SHA-256:',hashlib.sha256(got21).hexdigest())

if __name__=='__main__':main()
