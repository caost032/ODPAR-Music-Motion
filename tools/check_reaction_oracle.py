#!/usr/bin/env python3
"""Independent ODPAR 0.14 Reaction Matrix oracle.

Reconstructs routing semantics without importing expected values from the C
implementation.  The C probe is only the system under test.
"""
from __future__ import annotations
import argparse, hashlib, pathlib, subprocess, tempfile

Q=2147483647
SRC_MAX=25
A_ONLY=0
OPS=(1,2,3,4,5) # MAX/MIN/AVERAGE/MUL/ABS_DIFF

PROBE=r'''#include "compositor_internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t qsrc(uint32_t s){return (uint32_t)(((uint64_t)INT32_MAX*s+13u)/27u);}
static void states(odm_composition_frame_state*c,odm_director_frame_state*d){
 uint32_t i; memset(c,0,sizeof(*c)); memset(d,0,sizeof(*d));
 c->schema_version=ODM_COMPOSITION_SCHEMA_VERSION;c->mode=ODM_COMPOSITION_MODE_FLOW;c->tick_index=7u;
 c->core_breath_q31=qsrc(1);c->grid_q31=qsrc(2);c->particles_q31=qsrc(3);c->radial_gain_q31=qsrc(4);
 c->fracture_q31=qsrc(5);c->memory_echo_q31=qsrc(6);c->halo_q31=qsrc(7);c->lateral_field_q31=qsrc(8);c->accent_q31=qsrc(9);
 for(i=0u;i<6u;++i)c->band_q31[i]=qsrc(10u+i);
 d->schema_version=ODM_DIRECTOR_SCHEMA_VERSION;d->tick_index=7u;d->layout=ODM_DIRECTOR_LAYOUT_ORBIT;
 d->macro_energy_q31=qsrc(16);d->macro_novelty_q31=qsrc(17);d->depth_q31=qsrc(18);d->edge_activity_q31=qsrc(19);
 d->orbit_q31=qsrc(20);d->wings_q31=qsrc(21);d->memory_panels_q31=qsrc(22);d->tunnel_q31=qsrc(23);d->backdrop_q31=qsrc(24);d->architecture_q31=qsrc(25);
}
int main(void){
 odm_composition_frame_state c; odm_director_frame_state d; uint32_t a,b,op,r=0,v=0; odm_status st;
 states(&c,&d);
 for(a=0u;a<=ODM_REACTION_SOURCE_MAX;++a){
   st=odm_reaction_route_make(a,ODM_REACTION_SOURCE_NONE,ODM_REACTION_COMBINE_A_ONLY,&r);
   if(st!=ODM_STATUS_OK)return 2;
   st=odm_layered_reaction_eval(r,&c,&d,&v);
   if(st!=ODM_STATUS_OK)return 3;
   printf("A,%u,%u\n",a,v);
 }
 for(op=ODM_REACTION_COMBINE_MAX;op<=ODM_REACTION_COMBINE_ABS_DIFF;++op){
  for(a=1u;a<=ODM_REACTION_SOURCE_MAX;++a){
   for(b=1u;b<=ODM_REACTION_SOURCE_MAX;++b){
    st=odm_reaction_route_make(a,b,op,&r);if(st!=ODM_STATUS_OK)return 4;
    st=odm_layered_reaction_eval(r,&c,&d,&v);if(st!=ODM_STATUS_OK)return 5;
    printf("B,%u,%u,%u,%u\n",op,a,b,v);
   }
  }
 }
 r=UINT32_C(0x12345678);
 st=odm_reaction_route_make(ODM_REACTION_SOURCE_GRID,ODM_REACTION_SOURCE_NONE,ODM_REACTION_COMBINE_MAX,&r);
 printf("I,binary_none,%d,%u\n",(int)st,r);
 r=UINT32_C(0x80000002);v=UINT32_C(0x89abcdef);
 st=odm_layered_reaction_eval(r,&c,&d,&v);printf("I,high_bits,%d,%u\n",(int)st,v);
 r=0u; if(odm_reaction_route_make(ODM_REACTION_SOURCE_BAND_0,0u,0u,&r)!=ODM_STATUS_OK)return 6;
 c.band_q31[0]=UINT32_C(0x80000000);v=UINT32_C(0x76543210);
 st=odm_layered_reaction_eval(r,&c,&d,&v);printf("I,bad_q31,%d,%u\n",(int)st,v);
 return 0;
}
'''

def qsrc(s:int)->int: return (Q*s+13)//27

def mulq(a:int,b:int)->int: return min(Q,(a*b+Q//2)//Q)

def combine(op:int,a:int,b:int)->int:
    if op==1:return max(a,b)
    if op==2:return min(a,b)
    if op==3:return (a+b+1)//2
    if op==4:return mulq(a,b)
    if op==5:return abs(a-b)
    raise ValueError(op)

def main()->int:
    ap=argparse.ArgumentParser();ap.add_argument('--root',required=True);ap.add_argument('--cc',default='gcc');ap.add_argument('--library',required=True);a=ap.parse_args()
    root=pathlib.Path(a.root).resolve();lib=pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-reaction-oracle-') as td:
        td=pathlib.Path(td);src=td/'p.c';exe=td/'p';src.write_text(PROBE+'\n')
        include_dirs=[
            root/'include', root/'src/compositor', root/'src/renderer', root/'src/ir',
            root/'src/music_map', root/'src/visual', root/'src/core'
        ]
        cmd=[a.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Werror']
        for inc in include_dirs:
            cmd += ['-I', str(inc)]
        cmd += [str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        r=subprocess.run(cmd,cwd=root,capture_output=True,text=True)
        if r.returncode:raise SystemExit('reaction oracle compile failed\n'+r.stdout+r.stderr)
        x=subprocess.run([str(exe)],cwd=root,capture_output=True,text=True)
        if x.returncode:raise SystemExit(f'reaction oracle probe failed rc={x.returncode}\n{x.stdout}\n{x.stderr}')
    seen_a=0;seen_b=0;stream=bytearray()
    for line in x.stdout.splitlines():
        p=line.split(',')
        if p[0]=='A':
            s=int(p[1]);got=int(p[2]);exp=0 if s==0 else qsrc(s)
            if got!=exp:raise SystemExit(f'A_ONLY source {s} mismatch got={got} exp={exp}')
            stream += f'A,{s},{got}\n'.encode();seen_a+=1
        elif p[0]=='B':
            op,sa,sb,got=map(int,p[1:]);exp=combine(op,qsrc(sa),qsrc(sb))
            if got!=exp:raise SystemExit(f'combine mismatch op={op} a={sa} b={sb} got={got} exp={exp}')
            stream += f'B,{op},{sa},{sb},{got}\n'.encode();seen_b+=1
        elif p[0]=='I':
            name=p[1];st=int(p[2]);sent=int(p[3])
            if name=='binary_none' and (st!=1 or sent!=0x12345678):raise SystemExit('binary NONE builder not transactional/invalid')
            if name=='high_bits' and (st!=12 or sent!=0x89abcdef):raise SystemExit('high-bit route not fail-closed/transactional')
            if name=='bad_q31' and (st!=12 or sent!=0x76543210):raise SystemExit('non-Q31 selected signal not fail-closed/transactional')
            stream += (line+'\n').encode()
    if seen_a!=26 or seen_b!=5*25*25:raise SystemExit(f'coverage mismatch A={seen_a} B={seen_b}')
    print('REACTION MATRIX ORACLE: PASS')
    print(f'  A_ONLY sources checked: {seen_a}')
    print(f'  binary combinations checked: {seen_b}')
    print('  operators: MAX/MIN/AVERAGE/MUL/ABS_DIFF')
    print('  hostile routes + selected non-Q1.31 signals: fail-closed and output-transactional')
    print(f'  stream sha256={hashlib.sha256(stream).hexdigest()}')
    return 0
if __name__=='__main__':raise SystemExit(main())
