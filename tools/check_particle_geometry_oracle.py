#!/usr/bin/env python3
"""Independent pixel-exact oracle for ODPAR 0.13 particle geometry.

The C implementation is only the observed system. Python independently rebuilds
sample-authoritative phase motion, SplitMix64-derived particle identity, CORDIC
sin/cos, subpixel disk coverage and premultiplied RGBA16 blending.
"""
from __future__ import annotations

import argparse
import math
import pathlib
import struct
import subprocess
import tempfile

Q = 2147483647
MASK32 = (1 << 32) - 1
MASK64 = (1 << 64) - 1
W = H = 64
PARTICLES = 5
SAMPLES = [0, 1, 479, 480, 24000, 1234567, (1 << 63) - 1 - 4096]
SEED = 0x0D130D130D130D13
RING_PHASE = 0x13579BDF
PRIMARY = (50000, 30000, 10000, 60000)

ATAN = [
    536870912,316933406,167458907,85004756,42667331,21354465,10679838,5340245,
    2670163,1335087,667544,333772,166886,83443,41722,20861,10430,5215,2608,
    1304,652,326,163,81,41,20,10,5,3,1,1,
]
CORDIC_K = 1304065748
QUARTER = 0x40000000

PROBE = r'''
#include "odm_compositor.h"
#include "compositor_internal.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t qr(uint32_t n,uint32_t d){
    return (uint32_t)(((uint64_t)INT32_MAX*n+d/2u)/d);
}

static void make_config(odm_layered_config *c){
    memset(c,0,sizeof(*c));
    c->schema_version=ODM_LAYERED_SCHEMA_VERSION;
    c->canvas.schema_version=ODM_LAYERED_SCHEMA_VERSION;
    c->canvas.aspect=ODM_CANVAS_ASPECT_SQUARE_1_1;
    c->canvas.width=64u;c->canvas.height=64u;c->canvas.fps=30;
    c->background.schema_version=ODM_LAYERED_SCHEMA_VERSION;
    c->background.style=ODM_BACKGROUND_NONE;
    c->background.opacity_q31=(uint32_t)INT32_MAX;
    c->core.schema_version=ODM_LAYERED_SCHEMA_VERSION;
    c->core.shape=ODM_CORE_SHAPE_CIRCLE;
    c->core.fit=ODM_CORE_FIT_STRETCH;
    c->core.center_x_q31=qr(1u,2u);c->core.center_y_q31=qr(1u,2u);
    c->core.width_q31=qr(1u,4u);c->core.height_q31=qr(1u,4u);
    c->core.feather_q16=1u<<16;c->core.opacity_q31=(uint32_t)INT32_MAX;
    c->field.schema_version=ODM_LAYERED_SCHEMA_VERSION;
    c->field.flags=ODM_FIELD_PARTICLES;
    c->field.radial_segments=ODM_COMPOSITION_RADIAL_SEGMENTS;
    c->field.particle_count=5u;
    c->field.ring_gap_q16=1u<<16;
    c->field.particle_radius_q16=1u<<16;
    c->field.field_opacity_q31=qr(3u,4u);
    c->field.primary_color.r=50000u;c->field.primary_color.g=30000u;
    c->field.primary_color.b=10000u;c->field.primary_color.a=60000u;
    c->field.seed=UINT64_C(0x0d130d130d130d13);
    c->hud.schema_version=ODM_LAYERED_SCHEMA_VERSION;
    c->hud.text_scale_q16=1u<<16;
    c->hud.opacity_q31=(uint32_t)INT32_MAX;
}

int main(int argc,char **argv){
    static const int64_t samples[7]={
        INT64_C(0),INT64_C(1),INT64_C(479),INT64_C(480),INT64_C(24000),
        INT64_C(1234567),INT64_MAX-INT64_C(4096)
    };
    odm_layered_config c;
    odm_composition_frame_state co;
    odm_director_frame_state di;
    odm_layered_frame_plan p;
    odm_layered_pixel16 *frame;
    FILE *f;
    size_t vi;
    if(argc!=2)return 2;
    make_config(&c);
    if(odm_layered_config_validate(&c)!=ODM_STATUS_OK)return 3;
    frame=(odm_layered_pixel16*)calloc((size_t)64u*64u,sizeof(*frame));
    if(!frame)return 4;
    f=fopen(argv[1],"wb");if(!f){free(frame);return 5;}
    for(vi=0u;vi<7u;++vi){
        memset(&co,0,sizeof(co));memset(&di,0,sizeof(di));
        co.schema_version=ODM_COMPOSITION_SCHEMA_VERSION;
        co.mode=ODM_COMPOSITION_MODE_FLOW;
        co.tick_index=(uint64_t)samples[vi]/UINT64_C(480);
        co.center_sample=samples[vi];
        co.particles_q31=(uint32_t)INT32_MAX;
        co.ring_phase=UINT32_C(0x13579bdf);
        di.schema_version=ODM_DIRECTOR_SCHEMA_VERSION;
        di.tick_index=co.tick_index;
        di.layout=ODM_DIRECTOR_LAYOUT_MONOLITH;
        if(odm_layered_resolve_frame_plan(&c,&co,&di,samples[vi],INT64_MAX,&p)!=ODM_STATUS_OK){fclose(f);free(frame);return 6;}
        memset(frame,0,(size_t)64u*64u*sizeof(*frame));
        odm_layered_field_render(&c,&p,frame);
        if(fwrite(frame,sizeof(*frame),(size_t)64u*64u,f)!=(size_t)64u*64u){fclose(f);free(frame);return 7;}
    }
    if(fclose(f)!=0){free(frame);return 8;}
    free(frame);
    return 0;
}
'''


def qratio(n: int, d: int) -> int:
    return (Q * n + d // 2) // d


def cdiv(n: int, d: int) -> int:
    assert d > 0
    return n // d if n >= 0 else -((-n) // d)


def ratio_px_q16(dim: int, q31: int) -> int:
    return (dim * 65536 * q31 + Q // 2) // Q


def mulq(a: int, b: int) -> int:
    return min(Q, (a * b + Q // 2) // Q)


def mix64(x: int) -> int:
    x = (x + 0x9E3779B97F4A7C15) & MASK64
    x = ((x ^ (x >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    x = ((x ^ (x >> 27)) * 0x94D049BB133111EB) & MASK64
    return (x ^ (x >> 31)) & MASK64


def phase_advance(sample: int, speed: int) -> int:
    if sample <= 0:
        return 0
    whole = ((sample // 480) & MASK32) * speed
    frac = ((sample % 480) * speed) // 480
    return (whole + frac) & MASK32


def sin_q31(phase: int) -> int:
    phase &= MASK32
    quadrant = phase >> 30
    offset = phase & 0x3FFFFFFF
    sign = -1 if quadrant >= 2 else 1
    z = offset if quadrant in (0, 2) else QUARTER - offset
    x, y = CORDIC_K, 0
    if phase in (0, 0x80000000):
        return 0
    if phase == QUARTER:
        return Q
    if phase == 0xC0000000:
        return -Q
    for i, at in enumerate(ATAN):
        d = 1 << i
        xs, ys = cdiv(x, d), cdiv(y, d)
        if z >= 0:
            x, y, z = x - ys, y + xs, z - at
        else:
            x, y, z = x + ys, y - xs, z + at
    if sign < 0:
        y = -y
    return max(-Q, min(Q, y))


def cos_q31(phase: int) -> int:
    return sin_q31((phase + QUARTER) & MASK32)


def mul_u16_q31(v: int, q31: int) -> int:
    return min(65535, (v * q31 + Q // 2) // Q)


def mul_u16_u16(a: int, b: int) -> int:
    return (a * b + 32767) // 65535


def blend(dst, src, coverage, opacity):
    if coverage == 0 or opacity == 0:
        return dst
    q = (coverage * opacity + Q // 2) // Q
    a = mul_u16_q31(src[3], q)
    if a == 0:
        return dst
    sr = mul_u16_u16(src[0], a)
    sg = mul_u16_u16(src[1], a)
    sb = mul_u16_u16(src[2], a)
    inv = 65535 - a
    vals = []
    for sv, dv in zip((sr, sg, sb, a), dst):
        vals.append(min(65535, sv + (dv * inv + 32767) // 65535))
    return tuple(vals)


def disk_coverage(dist: int, radius_q16: int) -> int:
    feather = 65536
    sdf = dist - radius_q16
    if sdf <= -(feather // 2):
        return Q
    if sdf >= feather // 2:
        return 0
    return ((feather // 2 - sdf) * Q + feather // 2) // feather


def particle_plan(sample: int):
    center = ratio_px_q16(W, qratio(1, 2))
    base = ratio_px_q16(W, qratio(1, 4))
    radius = base // 2
    ring_inner = radius + (1 << 16)
    maxr = (min(W, H) * 65536 * 48) // 100 // 2
    rng = maxr - ring_inner
    field_opacity = qratio(3, 4)
    particle_gain = Q // 4 + mulq(Q, (Q * 3) // 4)
    count = (PARTICLES * particle_gain + Q // 2) // Q
    count = min(PARTICLES, count)
    tick = sample // 480
    return center, radius, ring_inner, maxr, rng, field_opacity, count, tick


def expected_frame(sample: int) -> bytes:
    center, radius, ring_inner, maxr, rng, field_opacity, count, tick = particle_plan(sample)
    assert rng > 0 and count == PARTICLES
    frame = [(0, 0, 0, 0) for _ in range(W * H)]
    for i in range(count):
        h = mix64(SEED ^ ((i * 0x9E3779B97F4A7C15) & MASK64))
        base = h & MASK32
        speed = 20000 + ((h >> 32) & 0xFFFF)
        phase = (base + phase_advance(sample, speed) + RING_PHASE) & MASK32
        rq = mix64(h ^ 0x5BF03635) >> 33
        pcs, psn = cos_q31(phase), sin_q31(phase)
        boundary = radius + (1 << 16)
        r = boundary + (rng * rq) // Q
        cx = center + cdiv(r * pcs, Q)
        cy = center + cdiv(r * psn, Q)
        flicker = mix64(h ^ tick) >> 33
        op = (field_opacity * (Q // 3 + flicker * 2 // 3) + Q // 2) // Q
        rad = 1 << 16
        feather = 1 << 16
        minx = (cx - rad - feather) >> 16
        maxx = (cx + rad + feather) >> 16
        miny = (cy - rad - feather) >> 16
        maxy = (cy + rad + feather) >> 16
        minx, miny = max(0, minx), max(0, miny)
        maxx, maxy = min(W - 1, maxx), min(H - 1, maxy)
        for y in range(miny, maxy + 1):
            for x in range(minx, maxx + 1):
                dx = ((x << 16) + 32768) - cx
                dy = ((y << 16) + 32768) - cy
                dist = math.isqrt(abs(dx) * abs(dx) + abs(dy) * abs(dy))
                cov = disk_coverage(dist, rad)
                if cov:
                    idx = y * W + x
                    frame[idx] = blend(frame[idx], PRIMARY, cov, op)
    return b''.join(struct.pack('<HHHH', *px) for px in frame)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--cc', default='gcc')
    ap.add_argument('--library', required=True)
    a = ap.parse_args()
    root = pathlib.Path(a.root).resolve()
    lib = pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-particle-oracle-') as td_s:
        td = pathlib.Path(td_s)
        src, exe, raw = td/'probe.c', td/'probe', td/'particles.bin'
        src.write_text(PROBE + '\n')
        cmd = [
            a.cc, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
            '-I', str(root/'include'), '-I', str(root/'src/compositor'), '-I', str(root/'src/renderer'), '-I', str(root/'src/ir'),
            str(src), str(lib), '-pthread', '-lpng16', '-lssl', '-lcrypto', '-o', str(exe),
        ]
        r = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit('particle oracle probe compile failed\n' + r.stdout + r.stderr)
        r = subprocess.run([str(exe), str(raw)], cwd=root, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f'particle oracle probe failed rc={r.returncode}\n{r.stdout}\n{r.stderr}')
        got = raw.read_bytes()
    frame_bytes = W * H * 8
    exp_all = bytearray()
    for sample in SAMPLES:
        exp_all.extend(expected_frame(sample))
    exp = bytes(exp_all)
    if len(got) != len(exp):
        raise SystemExit(f'particle stream size mismatch got={len(got)} exp={len(exp)}')
    if got != exp:
        j = next(i for i, (gv, ev) in enumerate(zip(got, exp)) if gv != ev)
        vec = j // frame_bytes
        off = j % frame_bytes
        pix = off // 8
        x, y = pix % W, pix // W
        ch = (off % 8) // 2
        go = got[vec*frame_bytes + pix*8:vec*frame_bytes + pix*8 + 8]
        eo = exp[vec*frame_bytes + pix*8:vec*frame_bytes + pix*8 + 8]
        raise SystemExit(
            f'particle raster mismatch vector={vec} sample={SAMPLES[vec]} '
            f'pixel=({x},{y}) channel={ch} got={go.hex()} exp={eo.hex()}'
        )
    import hashlib
    digest = hashlib.sha256(exp).hexdigest()
    print('PARTICLE GEOMETRY ORACLE: PASS')
    print(f'  vectors: {len(SAMPLES)} samples, including INT64_MAX-4096')
    print(f'  pixels: {len(SAMPLES)*W*H}, channels: {len(SAMPLES)*W*H*4}')
    print('  independently reconstructed: seed/mix64 + sample phase + CORDIC + disk feather + premul blend')
    print(f'  stream sha256={digest}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
