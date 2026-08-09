#!/usr/bin/env python3
"""Independent Gate-3 Music Map oracle.

Reimplements the frozen CORDIC/Hann/FFT/feature formulas with Python arbitrary
precision integers, then compares complete C-generated tables, policy bytes and
analysis.bin bytes.  It deliberately does not call engine math to derive expected
values.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import struct
import subprocess
import tempfile

INT32_MIN = -(1 << 31)
INT32_MAX = (1 << 31) - 1
UINT32_MAX = (1 << 32) - 1
INT64_MAX = (1 << 63) - 1
MASK32 = (1 << 32) - 1
CORDIC_K = 1304065748
QUARTER = 0x40000000
ATAN = [
    536870912,316933406,167458907,85004756,42667331,21354465,10679838,
    5340245,2670163,1335087,667544,333772,166886,83443,41722,20861,
    10430,5215,2608,1304,652,326,163,81,41,20,10,5,3,1,1,
]
N = 2048
TW = 1024
BINS = 1025
EDGES = [0,6,11,22,86,342,1025]
ENV = [4,16,64]
SILENCE = 2097152


def trunc_div(a: int, b: int) -> int:
    assert b > 0
    return -(abs(a) // b) if a < 0 else a // b


def c_remainder(a: int, b: int) -> int:
    return a - trunc_div(a,b) * b


def sin_q31(phase: int) -> int:
    phase &= MASK32
    q = phase >> 30
    off = phase & 0x3fffffff
    sign = -1 if q >= 2 else 1
    z = off if q in (0,2) else QUARTER - off
    x, y = CORDIC_K, 0
    for i, angle in enumerate(ATAN):
        xs = trunc_div(x, 1 << i)
        ys = trunc_div(y, 1 << i)
        if z >= 0:
            nx, ny = x - ys, y + xs
            z -= angle
        else:
            nx, ny = x + ys, y - xs
            z += angle
        x, y = nx, ny
    if sign < 0:
        y = -y
    return min(INT32_MAX, max(INT32_MIN, y))


def cos_q31(phase: int) -> int:
    return sin_q31((phase + QUARTER) & MASK32)


def round_half_i64(value: int) -> int:
    mag = abs(value)
    rounded = mag // 2 + (1 if mag & 1 else 0)
    if value < 0:
        if rounded >= 1 << 31:
            return INT32_MIN
        return -rounded
    return min(INT32_MAX, rounded)


def q31_mul(a: int, b: int) -> int:
    product = a * b
    mag = abs(product)
    rounded = (mag >> 31) + (1 if (mag & 0x7fffffff) >= 0x40000000 else 0)
    if product >= 0:
        rounded = min(rounded, INT32_MAX)
        return rounded
    if rounded >= 1 << 31:
        return INT32_MIN
    return -rounded


def energy_sample(v: int) -> int:
    square = abs(v) * abs(v)
    e = (square >> 31) + (1 if (square & 0x7fffffff) >= 0x40000000 else 0)
    return min(e, UINT32_MAX)


def isqrt_c(value: int) -> int:
    result = 0
    bit = 1 << 62
    while bit > value:
        bit >>= 2
    while bit:
        if value >= result + bit:
            value -= result + bit
            result = (result >> 1) + bit
        else:
            result >>= 1
        bit >>= 2
    return result


def rms(sum_energy: int, count: int) -> int:
    mean = (sum_energy + count // 2) // count
    root = isqrt_c(mean << 31)
    return min(root, INT32_MAX)


def magnitude_q31(re: int, im: int) -> int:
    total = abs(re)*abs(re) + abs(im)*abs(im)
    e = (total >> 31) + (1 if (total & 0x7fffffff) >= 0x40000000 else 0)
    e = min(e, UINT32_MAX)
    return min(isqrt_c(e << 31), UINT32_MAX)


def energy_complex(re: int, im: int) -> int:
    total = abs(re)*abs(re) + abs(im)*abs(im)
    e = (total >> 31) + (1 if (total & 0x7fffffff) >= 0x40000000 else 0)
    return min(e, UINT32_MAX)


def fft(values: list[tuple[int,int]], twiddles: list[tuple[int,int]]) -> list[tuple[int,int]]:
    vals = values[:]
    j = 0
    for i in range(1,N):
        bit = N >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            vals[i], vals[j] = vals[j], vals[i]
    length = 2
    while length <= N:
        half = length >> 1
        stride = N // length
        for base in range(0,N,length):
            for jj in range(half):
                wr, wi = twiddles[jj*stride]
                er, ei = vals[base+jj]
                or_, oi = vals[base+jj+half]
                ac=q31_mul(or_,wr); bd=q31_mul(oi,wi)
                ad=q31_mul(or_,wi); bc=q31_mul(oi,wr)
                rr=ac-bd; ri=ad+bc
                vals[base+jj]=(round_half_i64(er+rr), round_half_i64(ei+ri))
                vals[base+jj+half]=(round_half_i64(er-rr), round_half_i64(ei-ri))
        if length == N:
            break
        length <<= 1
    return vals


def fraction_to_q31(mag: int, den: int) -> int:
    q=0; rem=mag
    for _ in range(31):
        q <<= 1
        if rem >= den-rem:
            q |= 1
            rem -= den-rem
        else:
            rem += rem
    if rem and rem >= den-rem:
        q += 1
    return q


def ratio_q31(num: int, den: int) -> int:
    if den == 0 or den > INT64_MAX:
        return 0
    num=max(-den,min(den,num))
    mag=abs(num)
    if mag == den:
        return INT32_MIN if num < 0 else INT32_MAX
    enc=fraction_to_q31(mag,den)
    if num < 0:
        return INT32_MIN if enc == 1<<31 else -enc
    return min(enc,INT32_MAX)


def env_step(prev:int, cur:int, div:int)->int:
    delta=cur-prev
    adj=trunc_div(delta,div)
    rem=c_remainder(delta,div)
    if rem and abs(rem) >= div-abs(rem):
        adj += -1 if delta < 0 else 1
    return min(UINT32_MAX,max(0,prev+adj))


def pcm_vector(frames: int=1440) -> list[tuple[int,int]]:
    out=[]
    for i in range(frames):
        l = ((i * 2654435761) & 0x3fffffff) - 536870912
        r = ((i * 2246822519 + 123) & 0x1fffffff) - 268435456
        out.append((l,r))
    return out


def window_index(center:int, relative:int, frames:int):
    if relative < 0:
        mag=-relative
        if center < mag: return None
        idx=center-mag
    else:
        if center + relative >= 1<<64: return None
        idx=center+relative
    return idx if idx < frames else None


def analyze_tick(pcm, tick_index, window, twiddles, state):
    center=tick_index*480
    energies=[0,0,0,0]
    input_fft=[]
    for idx in range(N):
        rel=idx-1024
        ai=window_index(center,rel,len(pcm))
        if ai is None:
            l=r=0
        else:
            l,r=pcm[ai]
        mid=trunc_div(l+r,2)
        side=trunc_div(l-r,2)
        comps=[l,r,mid,side]
        for c in range(4): energies[c]+=energy_sample(comps[c])
        input_fft.append((q31_mul(mid,window[idx]),0))
    rr=[rms(e,N) for e in energies]
    spec=fft(input_fft,twiddles)
    band=[0]*6; total=0; flux=0; magsum=0; weighted=0; mags=[]
    for k in range(BINS):
        re,im=spec[k]
        m=magnitude_q31(re,im); e=energy_complex(re,im)
        mags.append(m); total+=e; magsum+=m; weighted+=m*k
        if state is not None and m > state['mags'][k]: flux += m-state['mags'][k]
        for b in range(6):
            if EDGES[b] <= k < EDGES[b+1]:
                band[b]+=e; break
    avg=lambda s,c: min(UINT32_MAX,(s+c//2)//c) if c else 0
    total_e=avg(total,BINS)
    bands=[avg(band[b],EDGES[b+1]-EDGES[b]) for b in range(6)]
    flux_e=0 if state is None else avg(flux,BINS)
    centroid=0
    if magsum:
        whole=weighted//magsum; rem=weighted%magsum
        frac=(rem*65536 + magsum//2)//magsum
        centroid=min(UINT32_MAX,whole*65536+frac)
    if state is None:
        es=em=el=total_e; delta=0
    else:
        es=env_step(state['es'],total_e,4)
        em=env_step(state['em'],total_e,16)
        el=env_step(state['el'],total_e,64)
        delta=total_e-state['total']
    silence=1 if rr[2] <= SILENCE else 0
    lrs=rr[0]+rr[1]
    bal=ratio_q31(rr[1]-rr[0],lrs)
    mss=rr[2]+rr[3]
    width=ratio_q31(rr[3],mss) & UINT32_MAX
    tick={
      'tick_index':tick_index,'center':center,'rms':rr,'bands':bands,'total':total_e,
      'es':es,'em':em,'el':el,'flux':flux_e,'centroid':centroid,'delta':delta,
      'silence':silence,'balance':bal,'width':width,'reserved':[0]*7,
    }
    new={'mags':mags,'total':total_e,'es':es,'em':em,'el':el}
    return tick,new


def pack_tick(t):
    b=bytearray()
    b += struct.pack('<QQ',t['tick_index'],t['center'])
    b += struct.pack('<iiii',*t['rms'])
    b += struct.pack('<'+'I'*6,*t['bands'])
    b += struct.pack('<IIIII',t['total'],t['es'],t['em'],t['el'],t['flux'])
    b += struct.pack('<I',t['centroid'])
    b += struct.pack('<q',t['delta'])
    b += struct.pack('<I',t['silence'])
    b += struct.pack('<iI',t['balance'],t['width'])
    b += struct.pack('<'+'I'*7,*t['reserved'])
    assert len(b)==128
    return bytes(b)


def canonical_pcm_bytes(pcm):
    return b''.join(struct.pack('<ii',l,r) for l,r in pcm)


def build_expected():
    window=[]
    for i in range(N):
        phase=(i << 21) & MASK32
        numerator=INT32_MAX-cos_q31(phase)
        window.append(round_half_i64(numerator))
    twiddles=[]
    for i in range(TW):
        phase=(i << 21) & MASK32
        twiddles.append((cos_q31(phase),sin_q31((-phase)&MASK32)))
    wb=b''.join(struct.pack('<i',v) for v in window)
    tb=b''.join(struct.pack('<ii',a,b) for a,b in twiddles)
    wh=hashlib.sha256(wb).digest(); th=hashlib.sha256(tb).digest()
    policy=bytearray()
    policy += b'ODMPOL1\0'
    # v2: las razones con signo se publican en rango simetrico
    # [-INT32_MAX, +INT32_MAX]. Ver src/music_map/analysis.c.
    vals=[2,48000,100,480,2048,1025,1,1,1,1,1,1024,64,512,30,23,25,6]
    policy += struct.pack('<'+'I'*len(vals),*vals)
    policy += struct.pack('<'+'I'*7,*EDGES)
    policy += struct.pack('<III',*ENV)
    policy += struct.pack('<I',SILENCE)
    policy += wh+th+struct.pack('<I',0)
    policy += bytes(192-len(policy))
    assert len(policy)==192
    ph=hashlib.sha256(policy).digest()
    pcm=pcm_vector()
    state=None; ticks=[]
    for i in range((len(pcm)+479)//480):
        t,state=analyze_tick(pcm,i,window,twiddles,state); ticks.append(t)
    pcm_b=canonical_pcm_bytes(pcm); pcm_h=hashlib.sha256(pcm_b).digest()
    payload=bytearray()
    payload += b'ODMMAP1\0'
    payload += struct.pack('<II',1,0)
    payload += struct.pack('<IIIIIIII',48000,100,480,2048,1025,6,128,0)
    payload += struct.pack('<QQ',len(ticks),len(pcm))
    payload += pcm_h+ph
    assert len(payload)==128
    payload += b''.join(pack_tick(t) for t in ticks)
    pd=hashlib.sha256(payload).digest()
    header=bytearray(64)
    header[:4]=b'ODMC'
    struct.pack_into('<HHIIHHIQ',header,4,1,0,0x50414d4d,0,1,0,0,len(payload))
    header[32:64]=pd
    record=bytes(header)+bytes(payload)
    return wb,tb,bytes(policy),record,ph

PROBE = r'''\
#include "odm_music_map.h"
#include "odm_hash.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_file(const char *path, const void *data, size_t n) {
    FILE *f=fopen(path,"wb"); if(!f) return 0;
    if(n && fwrite(data,1,n,f)!=n){fclose(f);return 0;}
    return fclose(f)==0;
}
static void put_i32_le(uint8_t *p,int32_t v){uint32_t u=(uint32_t)v;p[0]=(uint8_t)u;p[1]=(uint8_t)(u>>8);p[2]=(uint8_t)(u>>16);p[3]=(uint8_t)(u>>24);}
int main(int argc,char **argv){
    int32_t *w; odm_music_complex_q31 *tw; odm_music_analysis_scratch *scratch;
    odm_music_analysis_plan plan; odm_music_analysis_state st; odm_music_analysis_tick ticks[3];
    odm_pcm_stereo_q31 pcm[1440]; odm_sha256_digest pcm_sha, rec_sha; odm_sha256_context hc=ODM_SHA256_CONTEXT_INITIALIZER;
    uint8_t policy[ODM_MUSIC_POLICY_BYTES]; uint64_t preq=0,treq=0,rreq=0; uint8_t *rec; uint8_t *wb,*tb; uint64_t i; char path[4096];
    if(argc!=2) return 2;
    w=(int32_t*)calloc(ODM_MUSIC_WINDOW_SAMPLES,sizeof(*w)); tw=(odm_music_complex_q31*)calloc(ODM_MUSIC_FFT_TWIDDLES,sizeof(*tw)); scratch=(odm_music_analysis_scratch*)calloc(1,sizeof(*scratch));
    if(!w||!tw||!scratch) return 3;
    if(odm_music_analysis_plan_build(w,ODM_MUSIC_WINDOW_SAMPLES,tw,ODM_MUSIC_FFT_TWIDDLES,&plan)!=ODM_STATUS_OK) return 4;
    for(i=0;i<1440;i++){uint32_t l=(uint32_t)(i*UINT64_C(2654435761))&UINT32_C(0x3fffffff);uint32_t r=(uint32_t)(i*UINT64_C(2246822519)+123u)&UINT32_C(0x1fffffff);pcm[i].left=(int32_t)l-INT32_C(536870912);pcm[i].right=(int32_t)r-INT32_C(268435456);}
    if(odm_music_analysis_state_init(&st)!=ODM_STATUS_OK) return 5;
    if(odm_music_analyze_map(&plan,w,tw,pcm,1440,&st,scratch,ticks,3,&treq,NULL)!=ODM_STATUS_OK||treq!=3) return 6;
    if(odm_sha256_init(&hc)!=ODM_STATUS_OK) return 7;
    for(i=0;i<1440;i++){uint8_t b[8];put_i32_le(b,pcm[i].left);put_i32_le(b+4,pcm[i].right);if(odm_sha256_update(&hc,b,8)!=ODM_STATUS_OK)return 8;}
    if(odm_sha256_final(&hc,&pcm_sha)!=ODM_STATUS_OK) return 9;
    if(odm_music_policy_bytes(&plan,policy,sizeof(policy),&preq)!=ODM_STATUS_OK||preq!=sizeof(policy)) return 10;
    if(odm_music_map_record_write(&plan,1440,&pcm_sha,ticks,3,NULL,0,&rreq,NULL)!=ODM_STATUS_BUFFER_TOO_SMALL) return 11;
    rec=(uint8_t*)malloc((size_t)rreq);wb=(uint8_t*)malloc(ODM_MUSIC_WINDOW_SAMPLES*4u);tb=(uint8_t*)malloc(ODM_MUSIC_FFT_TWIDDLES*8u);if(!rec||!wb||!tb)return 12;
    if(odm_music_map_record_write(&plan,1440,&pcm_sha,ticks,3,rec,rreq,&rreq,&rec_sha)!=ODM_STATUS_OK)return 13;
    for(i=0;i<ODM_MUSIC_WINDOW_SAMPLES;i++)put_i32_le(wb+i*4u,w[i]);
    for(i=0;i<ODM_MUSIC_FFT_TWIDDLES;i++){put_i32_le(tb+i*8u,tw[i].real);put_i32_le(tb+i*8u+4u,tw[i].imag);}
    snprintf(path,sizeof(path),"%s/window.bin",argv[1]); if(!write_file(path,wb,ODM_MUSIC_WINDOW_SAMPLES*4u))return 14;
    snprintf(path,sizeof(path),"%s/twiddle.bin",argv[1]); if(!write_file(path,tb,ODM_MUSIC_FFT_TWIDDLES*8u))return 15;
    snprintf(path,sizeof(path),"%s/policy.bin",argv[1]); if(!write_file(path,policy,sizeof(policy)))return 16;
    snprintf(path,sizeof(path),"%s/analysis.bin",argv[1]); if(!write_file(path,rec,(size_t)rreq))return 17;
    free(tb);free(wb);free(rec);free(scratch);free(tw);free(w);return 0;
}
'''


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--root',default='.')
    ap.add_argument('--library',default='build/g3wire/libodpar_music.a')
    ap.add_argument('--cc',default=os.environ.get('CC','gcc'))
    args=ap.parse_args()
    root=pathlib.Path(args.root).resolve(); lib=(root/args.library).resolve()
    expected=build_expected(); names=['window.bin','twiddle.bin','policy.bin','analysis.bin']
    with tempfile.TemporaryDirectory(prefix='odm-analysis-oracle-') as td:
        td=pathlib.Path(td); src=td/'probe.c'; exe=td/'probe'; src.write_text(PROBE)
        cmd=[args.cc,'-std=c11','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wsign-conversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Werror',f'-I{root / "include"}',f'-I{root / "src/media"}',f'-I{root / "src/music_map"}',str(src),str(lib),'-pthread','-lpng16','-lssl','-lcrypto','-o',str(exe)]
        subprocess.run(cmd,check=True,cwd=root)
        subprocess.run([str(exe),str(td)],check=True,cwd=root)
        for name,exp in zip(names,expected[:4]):
            got=(td/name).read_bytes()
            if got != exp:
                first=next((i for i,(a,b) in enumerate(zip(got,exp)) if a!=b),min(len(got),len(exp)))
                raise SystemExit(f'{name}: mismatch at byte {first}; got={len(got)} expected={len(exp)}')
    ph=expected[4].hex()
    print(f'Music analysis oracle: pass (2048 Hann coefficients, 1024 complex twiddles, 3 exact feature ticks, full analysis.bin)')
    print(f'Music policy SHA-256: {ph}')
    print(f'analysis.bin SHA-256: {hashlib.sha256(expected[3]).hexdigest()}')

if __name__=='__main__': main()
