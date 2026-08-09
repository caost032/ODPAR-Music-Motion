#!/usr/bin/env python3
"""Independent oracle for Visual Director v3.

Reimplements the macro-layout grammar in Python integer arithmetic and compares
all exported fields against the C shared library. It deliberately does not call
Composition v1: input composition frames are generated independently so the
Director is verified as its own semantic layer.
"""
import argparse, ctypes as C, hashlib, json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
Q=2147483647
M32=(1<<32)-1
M64=(1<<64)-1

# Layouts / flags from public contract.
MONOLITH,ORBIT,WINGS,MEMORY,EXPAND,VOID,FRACTURE=range(7)
GLITCH=1; STRICT_CAUSAL=8
FLAG_SHIFT=1;FLAG_RECOVERY=2;FLAG_IMPACT=4

class CompositionFrame(C.Structure):
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

class DirectorState(C.Structure):
    _fields_=[('schema_version',C.c_uint32),('initialized',C.c_uint32),('next_tick_index',C.c_uint64),('seed',C.c_uint64),
              ('current_layout',C.c_uint32),('previous_layout',C.c_uint32),('dwell_ticks',C.c_uint32),('transition_ticks_left',C.c_uint32),
              ('novelty_accum_q31_ticks',C.c_uint64),('macro_energy_q31',C.c_uint32),('macro_width_q31',C.c_uint32),
              ('macro_memory_q31',C.c_uint32),('macro_fracture_q31',C.c_uint32),('phase',C.c_uint32),('layout_epoch',C.c_uint32),
              ('transition_total_ticks',C.c_uint32),('candidate_layout',C.c_uint32),('candidate_ticks',C.c_uint32),('reserved0',C.c_uint32)]

class DirectorFrame(C.Structure):
    _fields_=[('schema_version',C.c_uint32),('layout',C.c_uint32),('previous_layout',C.c_uint32),('flags',C.c_uint32),
              ('tick_index',C.c_uint64),('transition_q31',C.c_uint32),('macro_energy_q31',C.c_uint32),('macro_novelty_q31',C.c_uint32),
              ('core_extent_q31',C.c_uint32),('orbit_q31',C.c_uint32),('wings_q31',C.c_uint32),('memory_panels_q31',C.c_uint32),
              ('tunnel_q31',C.c_uint32),('architecture_q31',C.c_uint32),('backdrop_q31',C.c_uint32),('depth_q31',C.c_uint32),
              ('edge_activity_q31',C.c_uint32),('asymmetry_x_q31',C.c_int32),('asymmetry_y_q31',C.c_int32),('orbit_phase',C.c_uint32),
              ('layout_epoch',C.c_uint32),('layer_count',C.c_uint32),('pane_count',C.c_uint32),('reserved',C.c_uint32*8)]

# Exact Q1.31 constants from the C implementation.
C05=107374182; C08=171798692; C10=214748365; C12=257698038; C15=322122547; C18=386547057
C20=429496730; C25=536870912; C30=644245094; C35=751619277; C45=966367642; C48=1030792151
C50=1073741824; C55=1181116006; C58=1245540516; C60=1288490189; C65=1395864371; C70=1503238553
C80=1717986918; C90=1932735282
D02=42949673;D04=85899346;D08=171798692;D12=257698038;D22=472446402;D28=601295421;D38=816043786
D40=858993459;D44=944892805;D46=987842478;D52=1116691497;D56=1202590842;D62=1331439861;D68=1460288880
D72=1546188226;D78=1675037245;D85=1825361100;D88=1889785610;D92=1975684955
MIN_DWELL=1200; CANDIDATE_STABLE=180; FRACTURE_STABLE=10; FRACTURE_DWELL=1200; FRACTURE_RELEASE_STABLE=80; FRACTURE_MIN_HOLD=180
VOID_STABLE=20; RECOVERY_STABLE=10; STAGNATION_DWELL=2800
TRANS=180; FAST=70; NOVELTY_THRESHOLD=180*Q; STAGNATION_THRESHOLD=550*Q

def sat(v): return 0 if v<0 else Q if v>Q else int(v)
def mul(a,b): return sat((int(a)*int(b)+1073741823)//Q)
def add(a,b): return sat(int(a)+int(b))
def lerp(lo,hi,t): return lo if hi<=lo else add(lo,mul(hi-lo,t))
def smooth(cur,target,shift):
    d=int(target)-int(cur)
    # C signed integer division truncates toward zero.
    q=abs(d)//(1<<shift)
    if d<0:q=-q
    return sat(int(cur)+q)
def norm_range(v,lo,hi):
    if hi<=lo or v<=lo:return 0
    if v>=hi:return Q
    span=hi-lo
    return sat(((v-lo)*Q+span//2)//span)
def norm(v,ref):
    if ref==0 or v==0:return 0
    if v>=ref:return Q
    return sat((v*Q+ref//2)//ref)
def triangle(phase):
    phase&=M32;p=phase>>1
    if phase&0x80000000:p=M32-p
    v=((p>>1)-536870912)*2
    return max(-(1<<31),min((1<<31)-1,v))
def scaled_signed(w,g):
    p=int(w)*int(g);q=abs(p)//Q
    if p<0:q=-q
    return max(-(1<<31),min((1<<31)-1,q))
def hash32(seed,epoch):
    z=(seed + 0x9e3779b97f4a7c15*(epoch+1))&M64
    z^=z>>30;z=(z*0xbf58476d1ce4e5b9)&M64
    z^=z>>27;z=(z*0x94d049bb133111eb)&M64
    z^=z>>31
    return (z^(z>>32))&M32

def targets(layout):
    if layout==MONOLITH:return dict(core=D56,orbit=C15,wings=C12,memory=D08,tunnel=C20,architecture=C35,backdrop=C15,depth=C20,edge=C25,layers=2,panes=0)
    if layout==ORBIT:return dict(core=C48,orbit=D88,wings=C20,memory=C12,tunnel=C35,architecture=C60,backdrop=C20,depth=C45,edge=C60,layers=4,panes=0)
    if layout==WINGS:return dict(core=D46,orbit=C35,wings=D92,memory=C18,tunnel=C30,architecture=D52,backdrop=D22,depth=C50,edge=D78,layers=3,panes=2)
    if layout==MEMORY:return dict(core=D44,orbit=D28,wings=C25,memory=D92,tunnel=C55,architecture=C65,backdrop=C30,depth=D72,edge=C55,layers=3,panes=4)
    if layout==EXPAND:return dict(core=D40,orbit=D72,wings=C70,memory=C30,tunnel=D92,architecture=D85,backdrop=D38,depth=C80,edge=D85,layers=5,panes=2)
    if layout==VOID:return dict(core=D52,orbit=C05,wings=D02,memory=C20,tunnel=D08,architecture=C15,backdrop=D02,depth=C12,edge=C05,layers=1,panes=0)
    return dict(core=C45,orbit=D52,wings=D62,memory=D68,tunnel=C45,architecture=D78,backdrop=D28,depth=C60,edge=Q,layers=4,panes=3)
def candidate(c,energy,width,memory,fracture):
    if c['mode']==0 or c['void']>=C65:return VOID
    if (c['flags']&GLITCH) or fracture>=C45:return FRACTURE
    if memory>=C35:return MEMORY
    if width>=C55:return WINGS
    if energy>=C65:return EXPAND
    if c['radial']>=C55 or c['halo']>=C50:return ORBIT
    return MONOLITH
def alternate(current,seed,epoch):
    choices=[MONOLITH,ORBIT,WINGS,MEMORY,EXPAND];i=hash32(seed,epoch)%5
    for k in range(5):
        v=choices[(i+k)%5]
        if v!=current:return v
    return ORBIT
def blend(a,b,t): return lerp(a,b,t) if a<=b else a-mul(a-b,t)

def init(seed):
    return dict(initialized=0,next=0,seed=seed,current=MONOLITH,previous=MONOLITH,dwell=0,left=0,novacc=0,energy=0,width=0,memory=0,fracture=0,phase=(seed^(seed>>32)^0xa5a5a5a5)&M32,epoch=0,total=0,candidate=MONOLITH,candidate_ticks=0)

def step(s,c):
    if s['initialized'] and c['tick']!=s['next']:raise ValueError('sequence')
    if c['tick']==M64:raise OverflowError
    energy=norm_range(c['core'],C48,C58);width=norm_range(c['lateral'],C10,C80)
    memory=Q if c['memory']>=C70 else norm(c['memory'],C70)
    fracture=Q if c['fracture']>=C90 else norm(c['fracture'],C90)
    novelty=max(c['accent'],fracture)
    if c['flags']&GLITCH:novelty=Q
    impact=1 if (c['mode']==3 or c['flags']&GLITCH) else 0
    n=dict(s);shifted=0;recovery=0
    if not n['initialized']:
        desired=candidate(c,energy,width,memory,fracture)
        n.update(initialized=1,current=desired,previous=desired,candidate=desired,candidate_ticks=1,
                 dwell=1,energy=energy,width=width,memory=memory,fracture=fracture)
    else:
        n['energy']=smooth(n['energy'],energy,5);n['width']=smooth(n['width'],width,5);n['memory']=smooth(n['memory'],memory,4);n['fracture']=smooth(n['fracture'],fracture,3)
        if n['dwell']!=M32:n['dwell']+=1
        n['novacc']=min(M64,n['novacc']+novelty)
        desired=candidate(c,n['energy'],n['width'],n['memory'],n['fracture'])
        if desired==n['candidate']:
            if n['candidate_ticks']!=M32:n['candidate_ticks']+=1
        else:
            n['candidate']=desired;n['candidate_ticks']=1

        if n['candidate']==VOID and n['current']!=VOID and n['candidate_ticks']>=VOID_STABLE and n['dwell']>=VOID_STABLE:
            n.update(previous=n['current'],current=VOID,left=FAST,total=FAST,dwell=0,novacc=0,epoch=(n['epoch']+1)&M32);shifted=1
        elif n['current']==VOID and n['candidate']!=VOID and n['candidate_ticks']>=RECOVERY_STABLE and n['energy']>=C12:
            n.update(previous=n['current'],current=EXPAND,left=FAST,total=FAST,dwell=0,novacc=0,epoch=(n['epoch']+1)&M32);shifted=1;recovery=1
        elif n['current']==FRACTURE and n['candidate']!=FRACTURE and n['candidate_ticks']>=FRACTURE_RELEASE_STABLE and n['dwell']>=FRACTURE_MIN_HOLD:
            n.update(previous=n['current'],current=n['candidate'],left=FAST,total=FAST,dwell=0,novacc=0,epoch=(n['epoch']+1)&M32);shifted=1
        elif n['candidate']==FRACTURE and n['current']!=FRACTURE and n['candidate_ticks']>=FRACTURE_STABLE and n['dwell']>=FRACTURE_DWELL and c['fracture']>=D40:
            n.update(previous=n['current'],current=FRACTURE,left=FAST,total=FAST,dwell=0,novacc=0,epoch=(n['epoch']+1)&M32);shifted=1
        elif n['candidate']!=n['current'] and n['candidate_ticks']>=CANDIDATE_STABLE and n['dwell']>=MIN_DWELL and n['novacc']>=NOVELTY_THRESHOLD:
            n.update(previous=n['current'],current=n['candidate'],left=TRANS,total=TRANS,dwell=0,novacc=0,epoch=(n['epoch']+1)&M32);shifted=1
        elif n['candidate']==n['current'] and n['dwell']>=STAGNATION_DWELL and n['novacc']>=STAGNATION_THRESHOLD:
            desired=alternate(n['current'],n['seed'],n['epoch'])
            n.update(previous=n['current'],current=desired,candidate=desired,candidate_ticks=1,left=TRANS,total=TRANS,dwell=0,novacc=0,epoch=(n['epoch']+1)&M32);shifted=1
    phase_step=1048576+(n['energy']>>7)+(novelty>>9)
    n['phase']=(n['phase']+phase_step)&M32;n['next']=c['tick']+1
    if n['left']==0:
        transition=Q;n['total']=0
    else:
        total=n['total']
        if total==0 or n['left']>total:raise AssertionError('transition invariant')
        elapsed=total-n['left'];transition=(elapsed*Q+total//2)//total;n['left']-=1
    a=targets(n['previous']);b=targets(n['current'])
    core=blend(a['core'],b['core'],transition);shrink=mul(n['energy'],D04);core=core-shrink if core>shrink else D40
    orbit=mul(blend(a['orbit'],b['orbit'],transition),lerp(C25,Q,max(n['energy'],c['radial'])))
    wings=mul(blend(a['wings'],b['wings'],transition),lerp(C20,Q,n['width']))
    mem=mul(blend(a['memory'],b['memory'],transition),lerp(C15,Q,max(n['memory'],novelty//3)))
    tunnel=mul(blend(a['tunnel'],b['tunnel'],transition),lerp(C20,Q,n['energy']))
    arch=mul(blend(a['architecture'],b['architecture'],transition),lerp(C35,Q,n['energy']))
    backdrop=mul(blend(a['backdrop'],b['backdrop'],transition),lerp(C20,Q,max(n['memory'],n['width']//2)))
    depth=mul(blend(a['depth'],b['depth'],transition),lerp(C35,Q,max(n['width'],n['energy'])))
    edge=mul(blend(a['edge'],b['edge'],transition),lerp(C20,Q,max(n['energy'],novelty)))
    asym=mul(n['width'],C12);wx=triangle(n['phase']);wy=triangle((n['phase']+0x40000000)&M32)
    flags=(FLAG_SHIFT if shifted else 0)|(FLAG_RECOVERY if recovery else 0)|(FLAG_IMPACT if impact else 0)
    out=dict(schema_version=3,layout=n['current'],previous_layout=n['previous'],flags=flags,tick_index=c['tick'],transition_q31=transition,
             macro_energy_q31=n['energy'],macro_novelty_q31=novelty,core_extent_q31=core,orbit_q31=orbit,wings_q31=wings,memory_panels_q31=mem,
             tunnel_q31=tunnel,architecture_q31=arch,backdrop_q31=backdrop,depth_q31=depth,edge_activity_q31=edge,
             asymmetry_x_q31=scaled_signed(wx,asym),asymmetry_y_q31=scaled_signed(wy,asym//2),orbit_phase=(c['ring_phase']+n['phase'])&M32,
             layout_epoch=n['epoch'],layer_count=b['layers'],pane_count=b['panes'])
    return n,out

def synthetic(n=8400):
    """Long sections exercise all layouts; isolated impacts test anti-thrash."""
    out=[]
    for i in range(n):
        mode=2;flags=0;void=0
        core=1116691497;lateral=350000000;radial=700000000;halo=450000000
        memory=80000000;fracture=50000000;accent=650000000
        # 0..1299: MONOLITH. A one-tick glitch must not change macro layout.
        if i==600: flags|=GLITCH;mode=3;fracture=1500000000;accent=Q
        # 1300..2699: sustained WINGS.
        if 1300<=i<2700: lateral=1900000000;accent=700000000
        # 2700..4099: sustained MEMORY (higher priority than width).
        if 2700<=i<4100: memory=1700000000;lateral=400000000;accent=720000000
        # 4100..5499: sustained EXPAND.
        if 4100<=i<5500: core=C58;radial=900000000;halo=650000000;accent=850000000
        # 5500..6199: sustained FRACTURE; qualifies after 60 ticks.
        if 5500<=i<6200: fracture=1600000000;accent=1750000000
        # 6200..6299: true VOID.
        if 6200<=i<6300: mode=0;void=Q;core=C48;accent=0
        # 6300..6799: recovery is always EXPAND.
        if 6300<=i<6800: core=C58;radial=1950000000;halo=1650000000;accent=760000000
        # 6800..8399: sustained ORBIT (no width/memory/expand candidate).
        if 6800<=i: radial=1800000000;halo=1400000000;accent=780000000
        out.append(dict(tick=i,mode=mode,flags=flags,void=void,core=core,lateral=lateral,radial=radial,halo=halo,memory=memory,fracture=fracture,accent=accent,ring_phase=(i*2654435761)&M32))
    return out

def c_frame(d):
    f=CompositionFrame();f.schema_version=1;f.mode=d['mode'];f.flags=d['flags'];f.tick_index=d['tick'];f.center_sample=d['tick']*480
    f.core_scale_q31=d['core'];f.lateral_field_q31=d['lateral'];f.radial_gain_q31=d['radial'];f.halo_q31=d['halo'];f.memory_echo_q31=d['memory'];f.fracture_q31=d['fracture'];f.accent_q31=d['accent'];f.void_q31=d['void'];f.ring_phase=d['ring_phase']
    return f

def cdict(f):
    keys=['schema_version','layout','previous_layout','flags','tick_index','transition_q31','macro_energy_q31','macro_novelty_q31','core_extent_q31','orbit_q31','wings_q31','memory_panels_q31','tunnel_q31','architecture_q31','backdrop_q31','depth_q31','edge_activity_q31','asymmetry_x_q31','asymmetry_y_q31','orbit_phase','layout_epoch','layer_count','pane_count']
    return {k:getattr(f,k) for k in keys}

def cstate_dict(s):
    return dict(initialized=s.initialized,next=s.next_tick_index,seed=s.seed,current=s.current_layout,previous=s.previous_layout,
                dwell=s.dwell_ticks,left=s.transition_ticks_left,novacc=s.novelty_accum_q31_ticks,energy=s.macro_energy_q31,
                width=s.macro_width_q31,memory=s.macro_memory_q31,fracture=s.macro_fracture_q31,phase=s.phase,epoch=s.layout_epoch,
                total=s.transition_total_ticks,candidate=s.candidate_layout,candidate_ticks=s.candidate_ticks)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--shared',default=str(ROOT/'build/libodpar_music.so'));args=ap.parse_args()
    lib=C.CDLL(str(Path(args.shared)))
    lib.odm_director_init.argtypes=[C.POINTER(DirectorState),C.c_uint64];lib.odm_director_init.restype=C.c_int
    lib.odm_director_resolve_tick.argtypes=[C.POINTER(DirectorState),C.POINTER(CompositionFrame),C.POINTER(DirectorFrame)];lib.odm_director_resolve_tick.restype=C.c_int
    lib.odm_director_resolve_batch.argtypes=[C.POINTER(DirectorState),C.POINTER(CompositionFrame),C.c_uint64,C.POINTER(DirectorFrame),C.c_uint64];lib.odm_director_resolve_batch.restype=C.c_int
    assert C.sizeof(CompositionFrame)==1704 and C.sizeof(DirectorState)==88 and C.sizeof(DirectorFrame)==128
    seed=0xc011ec7100d1ec70;cs=DirectorState();assert lib.odm_director_init(C.byref(cs),seed)==0;ps=init(seed)
    seq=synthetic();sha=hashlib.sha256();layouts=set();shifts=recovers=impacts=0
    cframes=(CompositionFrame*len(seq))()
    for i,d in enumerate(seq):
        cframes[i]=c_frame(d);got=DirectorFrame();rc=lib.odm_director_resolve_tick(C.byref(cs),C.byref(cframes[i]),C.byref(got));assert rc==0,(i,rc)
        ps,exp=step(ps,d);gd=cdict(got)
        if gd!=exp:
            for k in gd:
                if gd[k]!=exp[k]:raise AssertionError((i,k,gd[k],exp[k]))
        sd=cstate_dict(cs)
        if sd!=ps:
            for k in sd:
                if sd[k]!=ps[k]:raise AssertionError((i,'state.'+k,sd[k],ps[k]))
        # The isolated glitch at tick 600 is an IMPACT micro-event, not a macro shift.
        if i==600:
            assert got.layout==MONOLITH and (got.flags&FLAG_SHIFT)==0 and (got.flags&FLAG_IMPACT)!=0
        sha.update(bytes(got));layouts.add(got.layout);shifts+=bool(got.flags&FLAG_SHIFT);recovers+=bool(got.flags&FLAG_RECOVERY);impacts+=bool(got.flags&FLAG_IMPACT)
    # Batch equality + transactional negative.
    bs=DirectorState();assert lib.odm_director_init(C.byref(bs),seed)==0;bout=(DirectorFrame*len(seq))();assert lib.odm_director_resolve_batch(C.byref(bs),cframes,len(seq),bout,len(seq))==0
    assert bytes(bout[-1])==bytes(got) and bs.next_tick_index==len(seq)
    sent=(DirectorFrame*len(seq))();C.memset(C.addressof(sent),0x5a,C.sizeof(sent));bad=cframes[17].tick_index;cframes[17].tick_index=99999
    ns=DirectorState();assert lib.odm_director_init(C.byref(ns),seed)==0;rc=lib.odm_director_resolve_batch(C.byref(ns),cframes,len(seq),sent,len(seq));assert rc!=0 and ns.initialized==0 and ns.next_tick_index==0
    assert bytes(sent)==b'\x5a'*C.sizeof(sent);cframes[17].tick_index=bad
    assert layouts==set(range(7)),sorted(layouts)
    assert recovers>=1 and shifts>=6

    # Strict-causal metamorphic lane. Two deliberately unrelated seeds and
    # hostile procedural phases must produce byte-identical Director frames
    # for identical musical evidence. Hold MONOLITH beyond stagnation dwell so
    # any seed-selected alternate would be exposed.
    strict_ticks=5000
    sa=DirectorState(); sb=DirectorState()
    assert lib.odm_director_init(C.byref(sa),0x0123456789abcdef)==0
    assert lib.odm_director_init(C.byref(sb),0xfedcba9876543210)==0
    strict_sha=hashlib.sha256(); strict_shifts=0
    for i in range(strict_ticks):
        d=dict(tick=i,mode=2,flags=STRICT_CAUSAL,void=0,core=1116691497,
               lateral=350000000,radial=700000000,halo=450000000,
               memory=80000000,fracture=50000000,accent=650000000,
               ring_phase=(0xdeadbeef + i*0x9e3779b9)&M32)
        cf=c_frame(d)
        cf.phase=(0x31415926 + i*0x7f4a7c15)&M32
        ga=DirectorFrame(); gb=DirectorFrame()
        assert lib.odm_director_resolve_tick(C.byref(sa),C.byref(cf),C.byref(ga))==0
        # Poison only procedural fields on the second input; musical evidence
        # and tick coordinates remain identical.
        cf.phase=(0x27182818 + i*0x6a09e667)&M32
        cf.ring_phase=(0x13579bdf + i*0xbb67ae85)&M32
        assert lib.odm_director_resolve_tick(C.byref(sb),C.byref(cf),C.byref(gb))==0
        if bytes(ga)!=bytes(gb): raise AssertionError((i,'strict_seed_or_phase_leak'))
        assert ga.asymmetry_x_q31==0 and ga.asymmetry_y_q31==0 and ga.orbit_phase==0
        assert ga.layout==MONOLITH and ga.layout_epoch==0
        strict_shifts += bool(ga.flags&FLAG_SHIFT)
        strict_sha.update(bytes(ga))
    assert strict_shifts==0

    result={'status':'pass','ticks':len(seq),'compared_fields_per_tick':40,'compared_values':len(seq)*40,'layouts_seen':sorted(layouts),'layout_shifts':shifts,'recoveries':recovers,'impact_ticks':impacts,'stream_sha256':sha.hexdigest(),'sizeof_composition_frame':C.sizeof(CompositionFrame),'sizeof_director_state':C.sizeof(DirectorState),'sizeof_director_frame':C.sizeof(DirectorFrame),'transactional_negative':'pass','strict_causal_ticks':strict_ticks,'strict_seed_phase_invariance':'pass','strict_stagnation_alternates':strict_shifts,'strict_stream_sha256':strict_sha.hexdigest()}
    print(json.dumps(result,sort_keys=True))
if __name__=='__main__':main()
