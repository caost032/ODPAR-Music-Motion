#!/usr/bin/env python3
"""Independent oracle for Visual Dynamics v1.

The whole response kernel is reconstructed here from docs/VISUAL_DYNAMICS_V1.md.
No production C source is parsed and no C output is used to derive an expected
value. The engine is observed only through the public odm_vd_* API.

Beyond byte-for-byte agreement on the envelope, this oracle checks the
metamorphic properties that make flicker structurally impossible: silence
produces stillness, the response is a function of the sample coordinate (so it
cannot depend on frame rate), seek equals playback, and the release is monotone.
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import tempfile

Q = 2147483647
SR = 48000

CLASSES = {
    0: ("RADIAL_TIP",     1,   0,  6,   55, 20,  64424509,  536870912, 0),
    1: ("CORE_IMPACT",    4,   6, 12,   90, 60, 107374182,  644245094, 128849018),
    2: ("HALO_BODY",     25,   0, 40,  180,  0,  64424509,  429496729, 0),
    3: ("PARTICLE_SPAWN", 0,   0,  0,  300, 40, 171798692,  751619277, 0),
    4: ("MEMORY_FIELD",   0,   0,  0, 1200,  0, 171798692,  858993459, 0),
    5: ("GRID_FIELD",   180,   0,  0,  700,  0, 107374182,  644245094, 0),
    6: ("BACKGROUND",   600,   0,  0, 2500,  0, 107374182,  858993459, 0),
}

PROBE = r'''#include "odm_visual_dynamics.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Drive the engine from a script on stdin so the oracle, not the probe, owns
   every decision about what to test.
     K <class>                 select kernel
     I <sample>                init state
     T <sample> <evidence>     offer a trigger, prints acceptance
     E <sample>                evaluate, prints level
     S                         print current state
*/
int main(void){
    odm_vd_kernel k; odm_vd_state s; char op;
    unsigned cls; unsigned long long a, b; uint32_t v, acc;
    if (odm_vd_kernel_class(0u,&k)!=ODM_STATUS_OK) return 2;
    if (odm_vd_state_init(&k,0u,&s)!=ODM_STATUS_OK) return 3;
    while (scanf(" %c",&op)==1) {
        if (op=='K') {
            if (scanf("%u",&cls)!=1) return 4;
            if (odm_vd_kernel_class(cls,&k)!=ODM_STATUS_OK) return 5;
            printf("K,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%u,%u\n",
                (unsigned long long)k.attack_samples,(unsigned long long)k.overshoot_samples,
                (unsigned long long)k.hold_samples,(unsigned long long)k.release_halflife_samples,
                (unsigned long long)k.refractory_samples,
                k.dead_zone_q31,k.knee_hi_q31,k.gain_q31,k.max_q31,k.overshoot_q31,k.floor_q31);
        } else if (op=='I') {
            if (scanf("%llu",&a)!=1) return 6;
            if (odm_vd_state_init(&k,(uint64_t)a,&s)!=ODM_STATUS_OK) return 7;
            printf("I\n");
        } else if (op=='T') {
            if (scanf("%llu %llu",&a,&b)!=2) return 8;
            acc=0u;
            if (odm_vd_trigger(&k,&s,(uint64_t)a,(uint32_t)b,&acc)!=ODM_STATUS_OK) return 9;
            printf("T,%u\n",acc);
        } else if (op=='E') {
            if (scanf("%llu",&a)!=1) return 10;
            v=0u;
            if (odm_vd_eval(&k,&s,(uint64_t)a,&v)!=ODM_STATUS_OK) return 11;
            printf("E,%u\n",v);
        } else if (op=='S') {
            printf("S,%llu,%llu,%u,%u\n",(unsigned long long)s.event_sample,
                (unsigned long long)s.trigger_sample,s.peak_q31,s.base_q31);
        } else if (op=='Q') {
            break;
        } else return 12;
    }
    return 0;
}
'''


def mul(a: int, b: int) -> int:
    return min(Q, (a * b + Q // 2) // Q)


def add(a: int, b: int) -> int:
    return min(Q, a + b)


def knee(v: int, lo: int, hi: int) -> int:
    if v <= lo:
        return 0
    if v >= hi or hi <= lo:
        return Q
    t = ((v - lo) * Q + (hi - lo) // 2) // (hi - lo)
    t2 = mul(t, t)
    t3 = mul(t2, t)
    return min(Q, max(0, 3 * t2 - 2 * t3))


# Frozen table of 2^(-i/256), rebuilt from the closed form (spec section 4.3).
EXP2 = [round(Q * (2.0 ** (-i / 256.0))) for i in range(257)]


def exp2_neg_frac(num: int, den: int) -> int:
    if den == 0 or num == 0:
        return Q
    if num >= den:
        return EXP2[256]
    scaled = (num * 256) // den
    frac = (num * 256) % den
    if scaled >= 256:
        return EXP2[256]
    lo, hi = EXP2[scaled], EXP2[scaled + 1]
    return lo - ((lo - hi) * frac + den // 2) // den


def release(peak: int, elapsed: int, halflife: int) -> int:
    if halflife == 0:
        return 0
    whole, frac = divmod(elapsed, halflife)
    if whole >= 32:
        return 0
    return mul(peak >> whole, exp2_neg_frac(frac, halflife))


class Kernel:
    def __init__(self, cls: int):
        (_, a_ms, o_ms, h_ms, r_ms, ref_ms, dz, kh, ov) = CLASSES[cls]
        ms = lambda m: (m * SR) // 1000
        self.attack = ms(a_ms)
        self.overshoot = ms(o_ms)
        self.hold = ms(h_ms)
        self.halflife = ms(r_ms)
        self.refractory = ms(ref_ms)
        self.dead_zone = dz
        self.knee_hi = kh
        self.gain = Q
        self.max = Q
        self.overshoot_q31 = ov
        self.floor = 0

    def wire(self) -> tuple:
        return (self.attack, self.overshoot, self.hold, self.halflife, self.refractory,
                self.dead_zone, self.knee_hi, self.gain, self.max, self.overshoot_q31,
                self.floor)

    def admit(self, evidence: int) -> int:
        peak = mul(mul(evidence, knee(evidence, self.dead_zone, self.knee_hi)), self.gain)
        return max(self.floor, min(self.max, peak))


class State:
    def __init__(self, kernel: Kernel, sample: int):
        self.event = sample
        self.trigger = 0
        self.peak = kernel.floor
        self.base = kernel.floor


def evaluate(k: Kernel, s: State, sample: int) -> int:
    if sample <= s.event:
        return s.base
    d = sample - s.event
    if d < k.attack:
        t = (d * Q + k.attack // 2) // k.attack
        shaped = knee(t, 0, Q)
        if s.peak >= s.base:
            level = add(s.base, mul(s.peak - s.base, shaped))
        else:
            drop = mul(s.base - s.peak, shaped)
            level = s.base - drop if s.base > drop else 0
    elif k.overshoot != 0 and d < k.attack + k.overshoot:
        e = d - k.attack
        x = (e * Q + k.overshoot // 2) // k.overshoot
        hump = min(Q, mul(x, Q - x) * 4)
        bump = mul(mul(s.peak, k.overshoot_q31), hump)
        level = add(s.peak, bump)
    elif d < k.attack + k.overshoot + k.hold:
        level = s.peak
    else:
        elapsed = d - k.attack - k.overshoot - k.hold
        if s.peak <= k.floor:
            level = k.floor
        else:
            level = add(k.floor, release(s.peak - k.floor, elapsed, k.halflife))
    return max(level, k.floor)


def trigger(k: Kernel, s: State, sample: int, evidence: int) -> int:
    peak = k.admit(evidence)
    current = evaluate(k, s, sample)
    if peak <= current:
        return 0
    within = (k.refractory != 0 and sample >= s.trigger
              and (sample - s.trigger) < k.refractory)
    if within and peak <= s.peak:
        return 0
    s.base = current
    s.peak = peak
    s.event = sample
    s.trigger = sample
    return 1


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--cc', default='gcc')
    ap.add_argument('--library', required=True)
    a = ap.parse_args()
    root = pathlib.Path(a.root).resolve()
    lib = pathlib.Path(a.library).resolve()

    script: list[str] = []
    expected: list[str] = []

    def emit(line: str, want: str | None = None) -> None:
        script.append(line)
        if want is not None:
            expected.append(want)

    # 1. Frozen class table.
    for cls in sorted(CLASSES):
        k = Kernel(cls)
        emit(f'K {cls}', 'K,' + ','.join(str(v) for v in k.wire()))

    # 2. SILENCE: no trigger ever, evaluated across a long span.
    k = Kernel(1)
    s = State(k, 0)
    emit('K 1', 'K,' + ','.join(str(v) for v in k.wire()))
    emit('I 0', 'I')
    for i in range(0, 400):
        sample = i * 977
        emit(f'E {sample}', f'E,{evaluate(k, s, sample)}')

    # 3. One impact, then no evidence ever again. The response must survive,
    #    decay monotonically, and reach rest without sticking.
    event = SR
    emit(f'T {event} {Q}', f'T,{trigger(k, s, event, Q)}')
    previous = None
    for i in range(0, 1500):
        sample = event + i * 61
        level = evaluate(k, s, sample)
        emit(f'E {sample}', f'E,{level}')
        tail_start = event + k.attack + k.overshoot + k.hold
        if previous is not None and sample > tail_start:
            if level > previous:
                raise SystemExit(f'release is not monotone at sample {sample}')
        previous = level

    # 4. FPS INVARIANCE: sampling the same response on six frame grids must give
    #    values that agree wherever the grids share a coordinate.
    by_rate: dict[int, dict[int, int]] = {}
    for rate in (24, 25, 30, 50, 60, 120):
        step = SR // rate
        by_rate[rate] = {}
        for i in range(0, 200):
            sample = i * step
            level = evaluate(k, s, sample)
            by_rate[rate][sample] = level
            emit(f'E {sample}', f'E,{level}')
    shared = set(by_rate[24]) & set(by_rate[60])
    if not shared:
        raise SystemExit('oracle fixture invalid: no shared 24/60 fps coordinates')
    for sample in shared:
        if by_rate[24][sample] != by_rate[60][sample]:
            raise SystemExit(f'frame rate leaked into the response at {sample}')

    # 5. NO DOUBLE FIRE and STRONGER WINS inside the refractory window.
    emit('I 0', 'I')
    s = State(k, 0)
    emit(f'T {SR} {Q}', f'T,{trigger(k, s, SR, Q)}')
    emit(f'T {SR + 1} {Q}', f'T,{trigger(k, s, SR + 1, Q)}')          # equal: rejected
    emit(f'T {SR + 2} {Q // 4}', f'T,{trigger(k, s, SR + 2, Q // 4)}')  # weaker: rejected
    emit('S', f'S,{s.event},{s.trigger},{s.peak},{s.base}')

    # A stronger event during a decayed tail is accepted, and the new response
    # departs continuously from the level the old one actually had.
    k2 = Kernel(0)
    s2 = State(k2, 0)
    emit('K 0', 'K,' + ','.join(str(v) for v in k2.wire()))
    emit('I 0', 'I')
    emit(f'T {SR} {Q // 2}', f'T,{trigger(k2, s2, SR, Q // 2)}')
    mid = SR + k2.attack + k2.hold + k2.halflife
    level_before = evaluate(k2, s2, mid)
    emit(f'E {mid}', f'E,{level_before}')
    emit(f'T {mid} {Q}', f'T,{trigger(k2, s2, mid, Q)}')
    emit(f'E {mid}', f'E,{evaluate(k2, s2, mid)}')
    if evaluate(k2, s2, mid) != level_before:
        raise SystemExit('retrigger introduced a discontinuity in the oracle model')
    emit('S', f'S,{s2.event},{s2.trigger},{s2.peak},{s2.base}')

    # 6. TIME SHIFT over every class.
    for cls in sorted(CLASSES):
        kc = Kernel(cls)
        emit(f'K {cls}', 'K,' + ','.join(str(v) for v in kc.wire()))
        for base_event in (0, 7 * SR + 331):
            sc = State(kc, 0)
            emit('I 0', 'I')
            emit(f'T {base_event + 5000} {Q}', f'T,{trigger(kc, sc, base_event + 5000, Q)}')
            for i in range(0, 120):
                sample = base_event + 5000 + i * 401
                emit(f'E {sample}', f'E,{evaluate(kc, sc, sample)}')
    emit('Q')

    with tempfile.TemporaryDirectory(prefix='odm-vd-') as td0:
        td = pathlib.Path(td0)
        src = td / 'probe.c'
        exe = td / 'probe'
        src.write_text(PROBE)
        cmd = [a.cc, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
               '-I', str(root / 'include'), str(src), str(lib),
               '-pthread', '-lpng16', '-lssl', '-lcrypto', '-o', str(exe)]
        c = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
        if c.returncode:
            raise SystemExit('visual dynamics probe compile failed\n' + c.stdout + c.stderr)
        r = subprocess.run([str(exe)], cwd=root, input='\n'.join(script) + '\n',
                           capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f'visual dynamics probe failed rc={r.returncode}\n{r.stderr}')

    got = [ln for ln in r.stdout.splitlines() if ln.strip()]
    if len(got) != len(expected):
        raise SystemExit(f'probe emitted {len(got)} lines, oracle expected {len(expected)}')
    for i, (g, e) in enumerate(zip(got, expected)):
        if g != e:
            raise SystemExit(f'visual dynamics mismatch at response {i}: got={g} expected={e}')

    print('VISUAL DYNAMICS ORACLE: PASS')
    print(f'  {len(expected)} responses reconstructed integer-exact from the specification')
    print('  silence produces stillness; one impact survives and decays monotonically to rest')
    print('  response is a function of the sample coordinate: 24/25/30/50/60/120 fps agree')
    print('  refractory rejects equal and weaker retriggers; a stronger event still wins')
    print('  retrigger mid-release is continuous: the new response departs from the current level')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
