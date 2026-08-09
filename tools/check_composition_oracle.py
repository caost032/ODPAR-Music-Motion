#!/usr/bin/env python3
import argparse, ctypes as C, hashlib, json, os, subprocess, sys, time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
QONE=2147483647
MASK32=(1<<32)-1

class Tick(C.Structure):
    _fields_=[('tick_index',C.c_uint64),('center_sample',C.c_uint64),
              ('rms_left_q31',C.c_int32),('rms_right_q31',C.c_int32),('rms_mid_q31',C.c_int32),('rms_side_q31',C.c_int32),
              ('band_energy_q31',C.c_uint32*6),('total_energy_q31',C.c_uint32),
              ('envelope_short_q31',C.c_uint32),('envelope_medium_q31',C.c_uint32),('envelope_long_q31',C.c_uint32),
              ('spectral_flux_q31',C.c_uint32),('spectral_centroid_bin_q16_16',C.c_uint32),('energy_delta_q31',C.c_int64),
              ('silence',C.c_uint32),('stereo_balance_q31',C.c_int32),('stereo_width_q31',C.c_uint32),('reserved',C.c_uint32*7)]
class Profile(C.Structure):
    _fields_=[('schema_version',C.c_uint32),('flags',C.c_uint32),('tick_count',C.c_uint64),('first_tick_index',C.c_uint64),('last_tick_index',C.c_uint64),
              ('envelope_short_ref',C.c_uint32),('envelope_medium_ref',C.c_uint32),('spectral_flux_ref',C.c_uint32),('reserved0',C.c_uint32),
              ('positive_delta_ref',C.c_uint64),('band_ref',C.c_uint32*6),('reserved',C.c_uint32*8)]

class State(C.Structure):
    _fields_=[('schema_version',C.c_uint32),('initialized',C.c_uint32),('next_tick_index',C.c_uint64),('seed',C.c_uint64),
              ('energy_slow_q31',C.c_uint32),('impact_hold_q31',C.c_uint32),('width_slow_q31',C.c_uint32),('high_slow_q31',C.c_uint32),
              ('glitch_cooldown_ticks',C.c_uint32),('phase',C.c_uint32),('reserved',C.c_uint32*6)]
class Frame(C.Structure):
    _fields_=[('schema_version',C.c_uint32),('mode',C.c_uint32),('flags',C.c_uint32),('reserved0',C.c_uint32),
              ('tick_index',C.c_uint64),('center_sample',C.c_uint64),('phase',C.c_uint32),('ring_phase',C.c_uint32),
              ('content_offset_x_q31',C.c_int32),('content_offset_y_q31',C.c_int32),
              ('core_scale_q31',C.c_uint32),('core_breath_q31',C.c_uint32),('core_border_q31',C.c_uint32),('halo_q31',C.c_uint32),
              ('radial_gain_q31',C.c_uint32),('radial_aperture_q31',C.c_uint32),('lateral_field_q31',C.c_uint32),('grid_q31',C.c_uint32),
              ('particles_q31',C.c_uint32),('memory_echo_q31',C.c_uint32),('fracture_q31',C.c_uint32),('chroma_split_q31',C.c_uint32),
              ('scan_q31',C.c_uint32),('vignette_q31',C.c_uint32),('void_q31',C.c_uint32),('accent_q31',C.c_uint32),
              ('band_q31',C.c_uint32*6),('radial_q31',C.c_uint32*96),
              ('radial_body_q31',C.c_uint32*96),('radial_release_q31',C.c_uint32*96),('radial_attack_q31',C.c_uint32*96),
              ('reserved',C.c_uint32*8)]

def sat(v): return 0 if v<0 else QONE if v>QONE else int(v)
def mul(a,b): return sat((int(a)*int(b)+1073741823)//2147483647)
def add(a,b): return sat(int(a)+int(b))
def lerp(lo,hi,t): return lo if hi<=lo else add(lo,mul(hi-lo,t))
def smooth(cur,target,shift): return sat(cur + int((target-cur)/(1<<shift)))
def tri(phase):
    phase &= MASK32
    p=phase>>1
    if phase & 0x80000000: p=MASK32-p
    v=((p>>1)-536870912)*2
    return max(-(1<<31),min((1<<31)-1,v))
def scaled_signed(w,g):
    # C truncates signed division toward zero
    p=int(w)*int(g)
    q=abs(p)//2147483647
    if p<0: q=-q
    return max(-(1<<31),min((1<<31)-1,q))

def py_init(seed):
    return dict(initialized=0,next=0,seed=seed,energy=0,impact=0,width=0,high=0,cool=0,phase=(seed^(seed>>32))&MASK32)

def py_step(s,t):
    if s['initialized'] and t['tick_index']!=s['next']: raise ValueError('sequence')
    if t['tick_index']==(1<<64)-1: raise OverflowError
    if t['silence'] not in (0,1): raise ValueError('silence')
    energy=min(QONE,t['env_m']); flux=min(QONE,t['flux'])
    pd=sat(t['delta']) if t['delta']>0 else 0
    impact=max(flux,pd); width=min(QONE,t['width']); low=min(QONE,max(t['bands'][0],t['bands'][1])); high=min(QONE,max(t['bands'][4],t['bands'][5]))
    n=dict(s)
    if not n['initialized']:
        n.update(initialized=1,energy=energy,width=width,high=high,impact=impact)
    else:
        n['energy']=smooth(n['energy'],energy,3); n['width']=smooth(n['width'],width,4); n['high']=smooth(n['high'],high,3)
        decay=n['impact']//18
        if decay==0 and n['impact']!=0: decay=1
        n['impact']=max(0,n['impact']-decay); n['impact']=max(n['impact'],impact)
    if n['cool']!=0: n['cool']-=1
    glitch=0
    if impact>=386547057 and n['cool']==0 and t['silence']==0:
        glitch=1; n['cool']=12
    step=6291456+(n['energy']>>8)+(n['width']>>10)
    n['phase']=(n['phase']+step)&MASK32; n['next']=t['tick_index']+1
    mode=0 if t['silence'] else (3 if n['impact']>=536870912 else (2 if n['energy']>=214748365 else 1))
    flags=(2 if t['silence'] else 0)|(1 if glitch else 0)
    ring=(n['phase'] + ((t['tick_index']*2654435761)&MASK32))&MASK32
    drift=mul(322122547, add(n['energy'],n['width'])//2)
    wx=tri(n['phase']); wy=tri((n['phase']+0x40000000)&MASK32)
    out=dict(schema_version=1,mode=mode,flags=flags,reserved0=0,tick_index=t['tick_index'],center_sample=t['center_sample'],phase=n['phase'],ring_phase=ring,
             content_offset_x_q31=scaled_signed(wx,drift),content_offset_y_q31=scaled_signed(wy,drift//2),
             core_scale_q31=lerp(1030792151,1245540516,n['energy']),core_breath_q31=mul(429496730,min(QONE,t['env_s'])),
             core_border_q31=lerp(536870912,1288490189,n['impact']),halo_q31=add(mul(low,1717986918),mul(n['energy'],536870912)),
             radial_gain_q31=lerp(386547057,1932735282,max(n['energy'],n['impact'])),radial_aperture_q31=lerp(966367642,1932735282,n['width']),
             lateral_field_q31=lerp(214748365,1717986918,n['width']),grid_q31=lerp(107374182,644245094,n['energy']),
             particles_q31=add(mul(n['high'],1932735282),mul(n['impact'],536870912)),memory_echo_q31=mul(n['impact'],1503238553),
             fracture_q31=mul(n['impact'],1932735282 if mode==3 else 751619277),chroma_split_q31=0,
             scan_q31=lerp(107374182,429496730,n['high']),vignette_q31=lerp(1181116006,1503238553,QONE-n['width']),
             void_q31=QONE if t['silence'] else mul(386547057,QONE-n['energy']),accent_q31=max(n['impact'],min(QONE,t['env_s'])),bands=[min(QONE,x) for x in t['bands']])
    out['chroma_split_q31']=mul(out['fracture_q31'],1932735282 if glitch else 536870912)
    radial=[]
    for i in range(48):
        b0=out['bands'][i%6]; b1=out['bands'][(i+1)%6]; mix=(b0//2+b1//2) if i&1 else b0
        phase_mod=(ring>>(i%24))&0xff; pq=phase_mod*8421504
        shaped=add(mul(mix,1717986918),mul(n['impact'],pq//5)); radial.append(mul(shaped,out['radial_gain_q31']))
    out['radial']=radial
    return n,out

def make_ticks(n=513):
    out=[]
    x=0x123456789abcdef0
    for i in range(n):
        x=(x*6364136223846793005+1442695040888963407)&((1<<64)-1)
        bands=[]
        for b in range(6):
            bands.append(20_000_000 + ((x>>(b*7))&0x1fffffff))
        envm=50_000_000+((x>>11)&0x2fffffff); envs=60_000_000+((x>>17)&0x3fffffff)
        flux=700_000_000 if i in (40,41,190,360,500) else 10_000_000+((x>>23)&0x07ffffff)
        delta=650_000_000 if i in (40,190,360,500) else -int((x>>31)&0x00ffffff)
        silence=1 if 270<=i<282 else 0
        out.append(dict(tick_index=i,center_sample=i*480,env_s=envs,env_m=envm,env_l=envm//2,flux=flux,delta=delta,width=40_000_000+((x>>29)&0x3fffffff),bands=bands,silence=silence))
    return out

def py_profile(ticks):
    return dict(schema_version=1,tick_count=len(ticks),first=ticks[0]['tick_index'],last=ticks[-1]['tick_index'],
                envs=max(t['env_s'] for t in ticks),envm=max(t['env_m'] for t in ticks),flux=max(t['flux'] for t in ticks),
                delta=max([t['delta'] for t in ticks if t['delta']>0] or [0]),bands=[max(t['bands'][b] for t in ticks) for b in range(6)])

def norm(v,ref):
    if ref==0 or v==0:return 0
    if v>=ref:return QONE
    return min(QONE,(v*QONE+ref//2)//ref)
def profiled_tick(t,p):
    d=dict(t);d['env_s']=norm(t['env_s'],p['envs']);d['env_m']=norm(t['env_m'],p['envm']);fq=norm(t['flux'],p['flux']);dq=norm(t['delta'],p['delta']) if t['delta']>0 else 0;d['flux']=mul(mul(fq,fq),fq);d['delta']=mul(mul(dq,dq),dq);d['bands']=[norm(t['bands'][b],p['bands'][b]) for b in range(6)];return d

def c_tick(d):
    t=Tick(); t.tick_index=d['tick_index'];t.center_sample=d['center_sample'];t.envelope_short_q31=d['env_s'];t.envelope_medium_q31=d['env_m'];t.envelope_long_q31=d['env_l'];t.spectral_flux_q31=d['flux'];t.energy_delta_q31=d['delta'];t.stereo_width_q31=d['width'];t.silence=d['silence']
    for i,v in enumerate(d['bands']):t.band_energy_q31[i]=v
    return t

def frame_dict(f):
    keys=['schema_version','mode','flags','reserved0','tick_index','center_sample','phase','ring_phase','content_offset_x_q31','content_offset_y_q31','core_scale_q31','core_breath_q31','core_border_q31','halo_q31','radial_gain_q31','radial_aperture_q31','lateral_field_q31','grid_q31','particles_q31','memory_echo_q31','fracture_q31','chroma_split_q31','scan_q31','vignette_q31','void_q31','accent_q31']
    d={k:getattr(f,k) for k in keys};d['bands']=list(f.band_q31);d['radial']=list(f.radial_q31)[:48];return d

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--shared',default=str(ROOT/'build/libodpar_music.so'))
    args=ap.parse_args()
    lib_path=Path(args.shared)
    if not lib_path.exists():
        raise SystemExit('missing shared library: '+str(lib_path))
    lib=C.CDLL(str(lib_path))
    lib.odm_composition_resolver_init.argtypes=[C.POINTER(State),C.c_uint64];lib.odm_composition_resolver_init.restype=C.c_int
    lib.odm_composition_resolve_tick.argtypes=[C.POINTER(State),C.POINTER(Tick),C.POINTER(Frame)];lib.odm_composition_resolve_tick.restype=C.c_int
    lib.odm_composition_profile_build.argtypes=[C.POINTER(Tick),C.c_uint64,C.POINTER(Profile)];lib.odm_composition_profile_build.restype=C.c_int
    lib.odm_composition_resolve_tick_profiled.argtypes=[C.POINTER(State),C.POINTER(Profile),C.POINTER(Tick),C.POINTER(Frame)];lib.odm_composition_resolve_tick_profiled.restype=C.c_int
    assert C.sizeof(Tick)==128 and C.sizeof(Profile)==112 and C.sizeof(State)==72 and C.sizeof(Frame)==1704
    cs=State(); assert lib.odm_composition_resolver_init(C.byref(cs),0x1122334455667788)==0
    ps=py_init(0x1122334455667788); ticks=make_ticks(); sha=hashlib.sha256(); gates=0
    for idx,d in enumerate(ticks):
        ct=c_tick(d);cf=Frame();rc=lib.odm_composition_resolve_tick(C.byref(cs),C.byref(ct),C.byref(cf));assert rc==0,(idx,rc)
        ps,exp=py_step(ps,d);got=frame_dict(cf)
        assert all(v==0 for v in list(cf.radial_q31)[48:]), ('legacy radial tail nonzero', idx)
        assert list(cf.radial_body_q31)[:48] == list(cf.radial_q31)[:48] and not any(list(cf.radial_body_q31)[48:]), ('legacy body mirror mismatch', idx)
        assert not any(cf.radial_release_q31) and not any(cf.radial_attack_q31), ('legacy temporal provenance nonzero', idx)
        if got!=exp:
            for k in got:
                if got[k]!=exp[k]: raise AssertionError((idx,k,got[k],exp[k]))
        sha.update(bytes(cf));gates += bool(cf.flags&1)
    # Profile build and fully independent profiled pass.
    arr=(Tick*len(ticks))(*[c_tick(d) for d in ticks]); cp=Profile(); assert lib.odm_composition_profile_build(arr,len(ticks),C.byref(cp))==0
    pp=py_profile(ticks)
    assert cp.tick_count==pp['tick_count'] and cp.first_tick_index==pp['first'] and cp.last_tick_index==pp['last']
    assert cp.envelope_short_ref==pp['envs'] and cp.envelope_medium_ref==pp['envm'] and cp.spectral_flux_ref==pp['flux'] and cp.positive_delta_ref==pp['delta'] and list(cp.band_ref)==pp['bands']
    cs2=State(); assert lib.odm_composition_resolver_init(C.byref(cs2),0x1122334455667788)==0
    ps2=py_init(0x1122334455667788); sha2=hashlib.sha256(); gates2=0
    for idx,d in enumerate(ticks):
        cf=Frame(); ct=arr[idx]; rc=lib.odm_composition_resolve_tick_profiled(C.byref(cs2),C.byref(cp),C.byref(ct),C.byref(cf)); assert rc==0,(idx,rc)
        nd=profiled_tick(d,pp); ps2,exp=py_step(ps2,nd); got=frame_dict(cf)
        assert all(v==0 for v in list(cf.radial_q31)[48:]), ('profiled legacy radial tail nonzero', idx)
        assert list(cf.radial_body_q31)[:48] == list(cf.radial_q31)[:48] and not any(list(cf.radial_body_q31)[48:]), ('profiled legacy body mirror mismatch', idx)
        assert not any(cf.radial_release_q31) and not any(cf.radial_attack_q31), ('profiled legacy temporal provenance nonzero', idx)
        if got!=exp:
            for k in got:
                if got[k]!=exp[k]: raise AssertionError(('profiled',idx,k,got[k],exp[k]))
        sha2.update(bytes(cf));gates2 += bool(cf.flags&1)
    result={'status':'pass','ticks':len(ticks),'fields_per_tick':26+6+48,'compared_values':2*len(ticks)*(26+6+48),'glitch_gates_raw':gates,'glitch_gates_profiled':gates2,'raw_stream_sha256':sha.hexdigest(),'profiled_stream_sha256':sha2.hexdigest(),'profile_refs':{'env_short':cp.envelope_short_ref,'env_medium':cp.envelope_medium_ref,'flux':cp.spectral_flux_ref,'positive_delta':cp.positive_delta_ref,'bands':list(cp.band_ref)},'sizeof_tick':C.sizeof(Tick),'sizeof_profile':C.sizeof(Profile),'sizeof_state':C.sizeof(State),'sizeof_frame':C.sizeof(Frame)}
    print(json.dumps(result,sort_keys=True))
if __name__=='__main__':main()
