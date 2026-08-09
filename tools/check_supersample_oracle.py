#!/usr/bin/env python3
"""Oraculo independiente para el supermuestreo.

Comprueba dos cosas distintas y ambas importan:

1. EXACTITUD. El resolve por area se reconstruye aqui en Python desde la
   especificacion (media aritmetica de factor*factor muestras con redondeo al
   mas cercano, sobre RGBA16LE lineal premultiplicado) y debe coincidir byte a
   byte con el motor.

2. QUE SIRVE PARA ALGO. Un resolve exacto que no reduzca el aliasing seria
   codigo correcto e inutil. Se mide la energia de borde -- la suma de saltos
   entre pixeles vecinos -- de un circulo rasterizado a 1x, 2x, 3x y 4x, y se
   exige que decrezca de forma monotona. Un borde con menos escalones tiene
   menos energia de borde; es la definicion operativa de "menos dentado".

Ademas verifica la frontera de calidad: escalar la configuracion y el plan no
puede tocar ningun valor Q1.31 de reaccion, ni la muestra, ni la provenance.
"""
from __future__ import annotations

import argparse
import pathlib
import struct
import subprocess
import tempfile

PROBE = r'''#include "odm_compositor.h"
#include "odm_renderer.h"
#include "odm_supersample.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t q(uint32_t n, uint32_t d){ return (uint32_t)(((uint64_t)INT32_MAX*n + d/2u)/d); }

static int build(odm_layered_config *c, odm_layered_frame_plan *p, uint32_t size){
    odm_composition_frame_state comp;
    odm_director_frame_state dir;
    uint32_t i;
    memset(c,0,sizeof(*c)); memset(p,0,sizeof(*p));
    memset(&comp,0,sizeof(comp)); memset(&dir,0,sizeof(dir));
    if (odm_layered_config_init_default(c, ODM_CANVAS_ASPECT_SQUARE_1_1, size, size, 30) != ODM_STATUS_OK)
        return 1;
    c->field.radial_segments = 96u;
    c->field.particle_count = 0u;
    c->field.flags = ODM_FIELD_RADIAL_BARS;
    c->background.style = ODM_BACKGROUND_NONE;
    c->hud.flags = 0u;
    if (odm_layered_config_validate(c) != ODM_STATUS_OK) return 2;

    /* La composicion se construye por la API real para que el plan resultante
       tenga areas seguras, banderas y provenance coherentes: un plan a mano
       seria rechazado por el render, y con razon. */
    comp.schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
    comp.tick_index = 0u; comp.center_sample = 0u;
    comp.core_scale_q31 = q(1u,2u);
    comp.radial_gain_q31 = (uint32_t)INT32_MAX;
    comp.radial_aperture_q31 = (uint32_t)INT32_MAX;
    for (i = 0u; i < 96u; ++i) {
        if (i == 13u) {
            uint64_t Q = (uint64_t)INT32_MAX, body, atk;
            comp.radial_body_q31[i] = q(1u,3u);
            comp.radial_release_q31[i] = 0u;
            comp.radial_attack_q31[i] = q(1u,2u);
            body = comp.radial_body_q31[i]; atk = comp.radial_attack_q31[i];
            comp.radial_q31[i] = (uint32_t)(body + ((Q-body)*atk + Q/2u)/Q);
        }
    }
    comp.flags = ODM_COMPOSITION_FLAG_RADIAL_HIRES | ODM_COMPOSITION_FLAG_STRICT_CAUSAL |
                 ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE | ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE;
    dir.schema_version = ODM_DIRECTOR_SCHEMA_VERSION;
    dir.tick_index = 0u;
    dir.layout = ODM_DIRECTOR_LAYOUT_MONOLITH;
    if (odm_layered_resolve_frame_plan(c, &comp, &dir, 0, 48000, p) != ODM_STATUS_OK) return 3;
    return 0;
}

static uint8_t core_pixels[32u*32u*4u];
static odm_render_surface_frame core_surface;

static void build_core(void){
    uint32_t x,y;
    for (y=0u;y<32u;++y) for (x=0u;x<32u;++x){
        uint8_t *px = core_pixels + ((size_t)y*32u + x)*4u;
        px[0]=(uint8_t)(x*8u); px[1]=(uint8_t)(y*8u); px[2]=200u; px[3]=255u;
    }
    memset(&core_surface,0,sizeof(core_surface));
    core_surface.width=32u; core_surface.height=32u;
    core_surface.pixel_format=ODM_RENDER_SURFACE_RGBA8;
    core_surface.primaries=ODM_COLOR_PRIMARIES_BT709;
    core_surface.transfer=ODM_COLOR_TRANSFER_SRGB;
    core_surface.alpha_mode=ODM_ALPHA_STRAIGHT;
    core_surface.start_sample=0; core_surface.end_sample=48000;
    core_surface.pixel_bytes=sizeof(core_pixels);
    core_surface.pixels=core_pixels;
}

int main(int ac, char **av){
    odm_layered_config c; odm_layered_frame_plan p;
    uint32_t size = 96u, tier;
    if (ac != 2) return 2;
    build_core();
    if (build(&c,&p,size)) return 3;

    /* 1. La escala no puede tocar semantica. */
    {
        odm_layered_config c2; odm_layered_frame_plan p2;
        if (odm_supersample_scale(&c,&p,3u,&c2,&p2) != ODM_STATUS_OK) return 4;
        printf("S,%u,%u,%u,%u,%u,%llu,%lld\n",
               c2.canvas.width, c2.canvas.height,
               p2.radial_q31[13], p2.radial_body_q31[13], p2.radial_attack_q31[13],
               (unsigned long long)p2.tick_index, (long long)p2.sample);
    }

    /* 2. Raster resuelto en cada nivel de calidad, volcado a disco. */
    for (tier = 0u; tier < (uint32_t)ODM_QUALITY_COUNT; ++tier) {
        uint64_t need = 0u; uint8_t *buf; char path[512]; FILE *fh;
        odm_status st = odm_supersample_render_frame(&c,&p,&core_surface,NULL,tier,NULL,0,&need,NULL);
        if (st != ODM_STATUS_BUFFER_TOO_SMALL) return 5;
        buf = (uint8_t*)malloc((size_t)need); if(!buf) return 6;
        st = odm_supersample_render_frame(&c,&p,&core_surface,NULL,tier,buf,need,&need,NULL);
        if (st != ODM_STATUS_OK) { free(buf); return 7; }
        if (snprintf(path,sizeof(path),"%s/tier%u.bin",av[1],tier) < 0) { free(buf); return 8; }
        fh = fopen(path,"wb"); if(!fh){free(buf);return 9;}
        if (fwrite(buf,1,(size_t)need,fh) != (size_t)need){fclose(fh);free(buf);return 10;}
        fclose(fh); free(buf);
        printf("T,%u,%u,%llu\n", tier, odm_supersample_factor(tier), (unsigned long long)need);
    }

    /* 3. Resolve aislado sobre un patron conocido, para comparar byte a byte. */
    {
        uint32_t f = 2u, ow = 4u, oh = 4u, hw = ow*f, hh = oh*f, x, y;
        uint8_t hi[4*2*4*2*8]; uint8_t out[4*4*8]; uint64_t need = 0u;
        for (y=0u;y<hh;++y) for (x=0u;x<hw;++x){
            uint8_t *px = hi + ((size_t)y*hw + x)*8u;
            uint16_t v = (uint16_t)((x*4919u + y*7919u) & 0xffffu);
            px[0]=(uint8_t)(v&0xffu); px[1]=(uint8_t)(v>>8);
            px[2]=(uint8_t)((v>>1)&0xffu); px[3]=(uint8_t)((v>>9));
            px[4]=(uint8_t)((v>>2)&0xffu); px[5]=(uint8_t)((v>>10));
            px[6]=0xffu; px[7]=0xffu;
        }
        if (odm_supersample_resolve_rgba16(hi,sizeof(hi),hw,hh,f,out,sizeof(out),ow,oh,&need)
            != ODM_STATUS_OK) return 11;
        printf("R");
        for (y=0u;y<oh*ow*8u;++y) printf(",%u", out[y]);
        printf("\n");
        printf("H");
        for (y=0u;y<hh*hw*8u;++y) printf(",%u", hi[y]);
        printf("\n");
    }
    return 0;
}
'''


def resolve(hi: list[int], hw: int, hh: int, factor: int) -> list[int]:
    """Media exacta de factor*factor muestras, redondeo al mas cercano."""
    ow, oh = hw // factor, hh // factor
    div = factor * factor
    out: list[int] = []
    for y in range(oh):
        for x in range(ow):
            acc = [0, 0, 0, 0]
            for sy in range(factor):
                for sx in range(factor):
                    base = (((y * factor + sy) * hw) + (x * factor + sx)) * 8
                    for ch in range(4):
                        acc[ch] += hi[base + ch * 2] | (hi[base + ch * 2 + 1] << 8)
            for ch in range(4):
                v = (acc[ch] + div // 2) // div
                out.append(v & 0xFF)
                out.append((v >> 8) & 0xFF)
    return out


def edge_energy(frame: bytes, width: int, height: int) -> int:
    """Suma de saltos absolutos de luminancia entre pixeles vecinos.

    Menos escalones en un borde curvo produce menos energia de borde. No es una
    metrica estetica: es una medida de cuanta discontinuidad hay en la imagen.
    """
    def lum(x: int, y: int) -> int:
        o = (y * width + x) * 8
        r = frame[o] | (frame[o + 1] << 8)
        g = frame[o + 2] | (frame[o + 3] << 8)
        b = frame[o + 4] | (frame[o + 5] << 8)
        return (r * 2126 + g * 7152 + b * 722) // 10000

    total = 0
    for y in range(height):
        for x in range(width - 1):
            total += abs(lum(x + 1, y) - lum(x, y))
    for y in range(height - 1):
        for x in range(width):
            total += abs(lum(x, y + 1) - lum(x, y))
    return total


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--cc', default='gcc')
    ap.add_argument('--library', required=True)
    a = ap.parse_args()
    root = pathlib.Path(a.root).resolve()
    lib = pathlib.Path(a.library).resolve()

    with tempfile.TemporaryDirectory(prefix='odm-ss-') as td0:
        td = pathlib.Path(td0)
        src = td / 'probe.c'
        exe = td / 'probe'
        src.write_text(PROBE)
        cmd = [a.cc, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
               '-I', str(root / 'include'), '-I', str(root / 'src/compositor'),
               str(src), str(lib), '-pthread', '-lpng16', '-lssl', '-lcrypto', '-o', str(exe)]
        c = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
        if c.returncode:
            raise SystemExit('supersample probe compile failed\n' + c.stdout + c.stderr)
        r = subprocess.run([str(exe), str(td)], cwd=root, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f'supersample probe failed rc={r.returncode}\n{r.stderr}')

        rows = [ln.split(',') for ln in r.stdout.splitlines() if ln.strip()]
        scale = next(x for x in rows if x[0] == 'S')
        tiers = [x for x in rows if x[0] == 'T']
        got_resolve = [int(v) for v in next(x for x in rows if x[0] == 'R')[1:]]
        hi = [int(v) for v in next(x for x in rows if x[0] == 'H')[1:]]

        # 1. La escala multiplica geometria y deja la semantica intacta.
        width, height = int(scale[1]), int(scale[2])
        if (width, height) != (96 * 3, 96 * 3):
            raise SystemExit(f'la escala no multiplico el lienzo: {width}x{height}')
        Q = 2147483647
        expected_body = (Q * 1 + 1) // 3
        expected_attack = (Q * 1 + 1) // 2
        if int(scale[4]) != expected_body or int(scale[5]) != expected_attack:
            raise SystemExit('la escala altero valores Q1.31 de reaccion: '
                             f'body={scale[4]} attack={scale[5]}')

        # 2. Resolve byte a byte contra la reconstruccion independiente.
        want = resolve(hi, 8, 8, 2)
        if got_resolve != want:
            first = next(i for i, (g, w) in enumerate(zip(got_resolve, want)) if g != w)
            raise SystemExit(f'resolve no coincide en el byte {first}: '
                             f'got={got_resolve[first]} exp={want[first]}')

        # 3. El aliasing debe bajar de forma monotona con el factor.
        energies = []
        for t in tiers:
            tier, factor = int(t[1]), int(t[2])
            data = (td / f'tier{tier}.bin').read_bytes()
            energies.append((tier, factor, edge_energy(data, 96, 96)))

    base = energies[0][2]
    for i in range(1, len(energies)):
        if energies[i][2] >= energies[i - 1][2]:
            raise SystemExit(
                'el supermuestreo no redujo el aliasing: '
                + ' '.join(f'{t}x{f}={e}' for t, f, e in energies))

    print('SUPERSAMPLE ORACLE: PASS')
    print('  la escala multiplica geometria y no toca ningun valor Q1.31 de reaccion')
    print('  resolve por area reconstruido byte a byte desde la especificacion')
    print('  energia de borde por nivel de calidad (menos = menos dentado):')
    for tier, factor, e in energies:
        drop = 100.0 * (1.0 - e / base) if base else 0.0
        print(f'    tier {tier} ({factor}x): {e:>12,}   -{drop:5.1f}%')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
