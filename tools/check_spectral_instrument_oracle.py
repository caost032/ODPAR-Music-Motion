#!/usr/bin/env python3
"""Oraculo independiente del instrumento espectral directo.

Reconstruye desde la especificacion -- nunca desde src/ -- y contrasta con el
motor:

1. LAS ANCLAS Y LOS BORDES. Se regeneran desde la ley declarada
   hz = 34*(15000/34)^(i/95), con bordes en i-1/2, usando decimal de 60 digitos.
   Deben coincidir exactamente con la tabla congelada del motor.

2. EL LOGARITMO. Se compara el log2 entero del motor contra el log2 decimal
   exacto sobre decenas de miles de valores. Se exige error <= 1 LSB de Q16.16 y
   monotonia no decreciente. La version anterior del instrumento usaba una
   mantisa lineal: aqui se mide cuanto costaba.

3. LA INTEGRACION POR BANDA. Se reconstruye la media exacta del espectro lineal
   a trozos sobre el intervalo de cada banda y se compara byte a byte con lo que
   el motor produce sobre espectros sinteticos conocidos.

4. QUE SIRVE PARA ALGO. Una integracion exacta que no mejorase la lectura seria
   codigo correcto e inutil. Se mide, sobre ruido reproducible:
     - cuanta energia real captura cada metodo,
     - cuanto tiembla cada metodo entre ventanas vecinas.
   Se exige que la integracion capture mas energia y tiemble menos en las bandas
   anchas. Es la definicion operativa de "lee mejor los agudos".
"""
from __future__ import annotations

import argparse
import pathlib
import random
import subprocess
import tempfile
from decimal import Decimal, getcontext, ROUND_HALF_UP

getcontext().prec = 60

Q31 = 2147483647
N = 2048
FS = 48000
BINS = N // 2 + 1
BANDS = 96
LOG_FLOOR = 15 << 16
LOG_CEIL = 27 << 16

PROBE = r'''#include "odm_spectral_instrument.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static odm_music_analysis_scratch scratch;

int main(int ac, char **av) {
    uint32_t i;
    odm_music_analysis_tick base;
    odm_spectral_instrument_tick tick;

    /* Tablas publicadas. */
    for (i = 0u; i < ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++i) {
        uint32_t anchor = 0u, bins = 0u;
        if (odm_spectral_instrument_band_bin_q16(i, &anchor) != ODM_STATUS_OK) return 2;
        if (odm_spectral_instrument_band_bins_q16(i, &bins) != ODM_STATUS_OK) return 3;
        printf("A,%u,%u,%u\n", i, anchor, bins);
    }
    for (i = 0u; i <= ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++i) {
        uint32_t e = 0u;
        if (odm_spectral_instrument_band_edge_q16(i, &e) != ODM_STATUS_OK) return 4;
        printf("E,%u,%u\n", i, e);
    }

    /* log2 sobre valores dados por el oraculo. */
    if (ac >= 2) {
        FILE *fh = fopen(av[1], "rb");
        unsigned long v;
        if (!fh) return 5;
        while (fscanf(fh, "%lu", &v) == 1)
            printf("L,%lu,%u\n", v, odm_spectral_instrument_log2_q16((uint32_t)v));
        fclose(fh);
    }

    /* Extraccion sobre espectros sinteticos leidos de fichero. */
    if (ac >= 3) {
        FILE *fh = fopen(av[2], "rb");
        uint32_t caso = 0u;
        if (!fh) return 6;
        while (1) {
            unsigned long m;
            uint32_t n = 0u;
            for (n = 0u; n < (uint32_t)ODM_MUSIC_FFT_BINS; ++n) {
                if (fscanf(fh, "%lu", &m) != 1) break;
                scratch.magnitude_q31[n] = (uint32_t)m;
            }
            if (n != (uint32_t)ODM_MUSIC_FFT_BINS) break;
            memset(&base, 0, sizeof(base));
            base.tick_index = caso;
            base.center_sample = (uint64_t)caso * (uint64_t)ODM_MUSIC_TICK_SAMPLES;
            if (odm_spectral_instrument_extract_tick(&base, &scratch, &tick) != ODM_STATUS_OK)
                return 7;
            printf("M,%u", caso);
            for (i = 0u; i < ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++i)
                printf(",%u", tick.band_magnitude_q31[i]);
            printf("\n");
            printf("P,%u", caso);
            for (i = 0u; i < ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++i)
                printf(",%u", tick.band_presence_q31[i]);
            printf("\n");
            ++caso;
        }
        fclose(fh);
    }

    /* Proyeccion centrada y coherencia de la provenance. */
    {
        odm_spectral_instrument_tick ts[3];
        odm_spectral_instrument_projection pr;
        odm_composition_frame_state frame;
        uint64_t s;
        memset(ts, 0, sizeof(ts));
        for (i = 0u; i < 3u; ++i) {
            uint32_t b;
            ts[i].schema_version = ODM_SPECTRAL_INSTRUMENT_SCHEMA_VERSION;
            ts[i].flags = ODM_SPECTRAL_INSTRUMENT_FLAG_SHARED_LOG_SCALE |
                          ODM_SPECTRAL_INSTRUMENT_FLAG_DIRECT_FFT |
                          ODM_SPECTRAL_INSTRUMENT_FLAG_BAND_INTEGRATION |
                          ODM_SPECTRAL_INSTRUMENT_FLAG_EXACT_LOG2;
            ts[i].tick_index = i;
            ts[i].center_sample = (uint64_t)i * (uint64_t)ODM_MUSIC_TICK_SAMPLES;
            for (b = 0u; b < ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++b)
                ts[i].band_presence_q31[b] =
                    (uint32_t)(((uint64_t)(b + 1u) * (i + 1u) * 7919u) % 2147483647u);
        }
        for (s = 0u; s <= 960u; s += 120u) {
            if (odm_spectral_instrument_project_centered(ts, 3u, s, &pr) != ODM_STATUS_OK)
                return 8;
            printf("J,%llu,%llu,%llu,%u,%u,%u\n",
                   (unsigned long long)s,
                   (unsigned long long)pr.lower_tick_index,
                   (unsigned long long)pr.upper_tick_index,
                   pr.upper_weight_q31, pr.band_presence_q31[0], pr.band_presence_q31[95]);
        }
        /* Aplicacion sobre un cuadro: el extremo y su explicacion. */
        if (odm_spectral_instrument_project_centered(ts, 3u, 540u, &pr) != ODM_STATUS_OK)
            return 9;
        memset(&frame, 0, sizeof(frame));
        frame.schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
        frame.center_sample = 480u;
        frame.flags = ODM_COMPOSITION_FLAG_RADIAL_HIRES | ODM_COMPOSITION_FLAG_STRICT_CAUSAL |
                      ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE | ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE;
        for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i) {
            frame.radial_q31[i] = 12345u;
            frame.radial_body_q31[i] = 999u;
            frame.radial_release_q31[i] = 888u;
            frame.radial_attack_q31[i] = 777u;
        }
        if (odm_composition_apply_spectral_instrument_projection(&pr, ODM_SPECTRAL_SYMMETRY_NONE, &frame) != ODM_STATUS_OK)
            return 10;
        {
            uint32_t coherente = 1u;
            for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i) {
                if (frame.radial_q31[i] != pr.band_presence_q31[i] ||
                    frame.radial_body_q31[i] != frame.radial_q31[i] ||
                    frame.radial_release_q31[i] != 0u ||
                    frame.radial_attack_q31[i] != 0u) { coherente = 0u; break; }
            }
            printf("V,%u,%u\n", coherente,
                   (frame.flags & ODM_COMPOSITION_FLAG_DIRECT_SPECTRAL_INSTRUMENT) ? 1u : 0u);
        }
        /* Una proyeccion lejos de la vecindad del cuadro debe rechazarse. */
        {
            odm_composition_frame_state lejos = frame;
            lejos.center_sample = 100000u;
            printf("R,%d\n",
                   (int)odm_composition_apply_spectral_instrument_projection(&pr, ODM_SPECTRAL_SYMMETRY_NONE, &lejos));
        }
    }
    return 0;
}
'''


def anchor_q16(i: int) -> int:
    r = (Decimal(15000) / Decimal(34)) ** (Decimal(1) / Decimal(95))
    hz = Decimal(34) * r ** Decimal(i)
    return int((hz * Decimal(N) / Decimal(FS) * 65536).to_integral_value(rounding=ROUND_HALF_UP))


def edge_q16(e: int) -> int:
    r = (Decimal(15000) / Decimal(34)) ** (Decimal(1) / Decimal(95))
    hz = Decimal(34) * r ** (Decimal(2 * e - 1) / Decimal(2))
    return int((hz * Decimal(N) / Decimal(FS) * 65536).to_integral_value(rounding=ROUND_HALF_UP))


def log2_exact_q16(x: int) -> Decimal:
    """log2 decimal exacto, en unidades de Q16.16."""
    return Decimal(x).ln() / Decimal(2).ln() * 65536


def div_round_away(num: int, den: int) -> int:
    q, r = abs(num) // abs(den), abs(num) % abs(den)
    if r * 2 >= abs(den):
        q += 1
    return -q if (num < 0) != (den < 0) else q


def band_mean(mag: list[int], lo: int, hi: int) -> int:
    """Media exacta del espectro lineal a trozos sobre [lo,hi) en bins Q16.16."""
    top = (BINS - 1) << 16
    lo, hi = min(lo, top), min(hi, top)
    if hi <= lo:
        base, t = lo >> 16, lo & 0xFFFF
        if base >= BINS - 1:
            return mag[BINS - 1]
        return mag[base] + div_round_away((mag[base + 1] - mag[base]) * t, 65536)
    acc = 0
    k, k_end = lo >> 16, (hi - 1) >> 16
    while k <= k_end:
        seg_lo, seg_hi = max(k << 16, lo), min((k + 1) << 16, hi)
        if seg_hi > seg_lo:
            u, v = seg_lo - (k << 16), seg_hi - (k << 16)
            slope = (mag[k + 1] - mag[k]) if k < BINS - 1 else 0
            val = mag[k] + div_round_away(slope * (u + v), 131072)
            acc += max(val, 0) * (seg_hi - seg_lo)
        k += 1
    den = hi - lo
    return min((acc + den // 2) // den, Q31)


def presence(mag: int, log2_engine) -> int:
    if mag == 0:
        return 0
    l = log2_engine(mag)
    if l <= LOG_FLOOR:
        return 0
    if l >= LOG_CEIL:
        return Q31
    rng = LOG_CEIL - LOG_FLOOR
    return ((l - LOG_FLOOR) * Q31 + rng // 2) // rng


def point_sample(mag: list[int], anchor_q16_v: int) -> int:
    """El metodo anterior: un unico punto interpolado por banda."""
    k, t = anchor_q16_v >> 16, anchor_q16_v & 0xFFFF
    if k >= BINS - 1:
        return mag[BINS - 1]
    return mag[k] + div_round_away((mag[k + 1] - mag[k]) * t, 65536)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--cc', default='gcc')
    ap.add_argument('--library', required=True)
    a = ap.parse_args()
    root = pathlib.Path(a.root).resolve()
    lib = pathlib.Path(a.library).resolve()

    rnd = random.Random(20260810)
    valores = [1, 2, 3, 4, 5, 7, 1023, 1024, 1025, 65535, 65536, 1 << 20, Q31, Q31 - 1]
    valores += [rnd.randrange(1, Q31) for _ in range(40000)]

    espectros: list[list[int]] = []
    # 1: rampa; 2: tonos aislados; 3: ruido; 4: ruido correlacionado (ventana vecina)
    espectros.append([min(Q31, i * 2000000) for i in range(BINS)])
    tono = [0] * BINS
    for c in (3, 40, 200, 500, 639):
        tono[c] = Q31 // 2
        tono[c - 1] = Q31 // 8
        tono[c + 1] = Q31 // 8
    espectros.append(tono)
    ruido_a = [rnd.randrange(0, Q31 // 4) for _ in range(BINS)]
    espectros.append(ruido_a)
    espectros.append([max(0, min(Q31, v + rnd.randrange(-(Q31 // 40), Q31 // 40)))
                      for v in ruido_a])

    with tempfile.TemporaryDirectory(prefix='odm-si-') as td0:
        td = pathlib.Path(td0)
        (td / 'log.txt').write_text('\n'.join(str(v) for v in valores))
        (td / 'spec.txt').write_text('\n'.join(
            ' '.join(str(v) for v in e) for e in espectros))
        src, exe = td / 'probe.c', td / 'probe'
        src.write_text(PROBE)
        cmd = [a.cc, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
               '-I', str(root / 'include'), str(src), str(lib),
               '-pthread', '-lpng16', '-lssl', '-lcrypto', '-o', str(exe)]
        c = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
        if c.returncode:
            raise SystemExit('spectral probe compile failed\n' + c.stdout + c.stderr)
        r = subprocess.run([str(exe), str(td / 'log.txt'), str(td / 'spec.txt')],
                           cwd=root, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f'spectral probe failed rc={r.returncode}\n{r.stderr}')

    filas = [ln.split(',') for ln in r.stdout.splitlines() if ln.strip()]

    # 1. Tablas.
    anclas = {int(f[1]): (int(f[2]), int(f[3])) for f in filas if f[0] == 'A'}
    bordes = {int(f[1]): int(f[2]) for f in filas if f[0] == 'E'}
    if len(anclas) != BANDS or len(bordes) != BANDS + 1:
        raise SystemExit('el motor no publica todas las bandas o bordes')
    for i in range(BANDS):
        if anclas[i][0] != anchor_q16(i):
            raise SystemExit(f'ancla {i}: motor={anclas[i][0]} ley={anchor_q16(i)}')
    for e in range(BANDS + 1):
        if bordes[e] != edge_q16(e):
            raise SystemExit(f'borde {e}: motor={bordes[e]} ley={edge_q16(e)}')
    for e in range(BANDS):
        if bordes[e] >= bordes[e + 1]:
            raise SystemExit(f'los bordes no son estrictamente crecientes en {e}')
        if anclas[e][1] != bordes[e + 1] - bordes[e]:
            raise SystemExit(f'la anchura publicada de la banda {e} no es su intervalo')
        if not (bordes[e] <= anclas[e][0] <= bordes[e + 1]):
            raise SystemExit(f'el ancla {e} cae fuera de su propio intervalo')

    # 2. Logaritmo.
    log_motor = {int(f[1]): int(f[2]) for f in filas if f[0] == 'L'}
    peor = Decimal(0)
    for v, got in log_motor.items():
        d = abs(log2_exact_q16(v) - Decimal(got))
        if d > peor:
            peor = d
    if peor > 1:
        raise SystemExit(f'el log2 del motor se desvia {peor} LSB del exacto')
    ordenados = sorted(log_motor)
    for x, y in zip(ordenados, ordenados[1:]):
        if log_motor[y] < log_motor[x]:
            raise SystemExit(f'el log2 no es monotono entre {x} y {y}')

    # 3. Integracion por banda, byte a byte.
    magnitudes = {int(f[1]): [int(x) for x in f[2:]] for f in filas if f[0] == 'M'}
    presencias = {int(f[1]): [int(x) for x in f[2:]] for f in filas if f[0] == 'P'}
    if len(magnitudes) != len(espectros):
        raise SystemExit('el motor no proceso todos los espectros de prueba')
    for caso, esp in enumerate(espectros):
        for b in range(BANDS):
            esperado = band_mean(esp, edge_q16(b), edge_q16(b + 1))
            if magnitudes[caso][b] != esperado:
                raise SystemExit(f'espectro {caso} banda {b}: motor='
                                 f'{magnitudes[caso][b]} oraculo={esperado}')
            pe = presence(esperado, lambda m: log_motor.get(m, int(log2_exact_q16(m))))
            if abs(presencias[caso][b] - pe) > 1:
                raise SystemExit(f'espectro {caso} banda {b}: presencia motor='
                                 f'{presencias[caso][b]} oraculo={pe}')

    # 4. Proyeccion centrada.
    for f in filas:
        if f[0] != 'J':
            continue
        s = int(f[1])
        lower = min(s // 480, 2)
        if int(f[2]) != lower:
            raise SystemExit(f'muestra {s}: tick inferior motor={f[2]} esperado={lower}')
        if s % 480 == 0 and int(f[4]) != 0 and s < 960:
            raise SystemExit(f'muestra {s} es un centro exacto y aun asi interpola')
    v = next(f for f in filas if f[0] == 'V')
    if int(v[1]) != 1:
        raise SystemExit('la provenance no describe el extremo que acompana')
    if int(v[2]) != 1:
        raise SystemExit('no se marco el gobierno del instrumento directo')
    rej = next(f for f in filas if f[0] == 'R')
    if int(rej[1]) == 0:
        raise SystemExit('una proyeccion fuera de la vecindad del cuadro fue aceptada')

    # 5. Que sirve para algo: energia capturada y temblor.
    ruido_a_esp, ruido_b_esp = espectros[2], espectros[3]
    ganancia_energia = []
    temblor_int, temblor_pt = [], []
    for b in range(BANDS):
        lo, hi = edge_q16(b), edge_q16(b + 1)
        if hi - lo < 4 * 65536:      # solo bandas anchas: las estrechas coinciden
            continue
        verdad = band_mean(ruido_a_esp, lo, hi)
        punto = point_sample(ruido_a_esp, anchor_q16(b))
        if verdad:
            ganancia_energia.append(abs(punto - verdad) / verdad)
        temblor_int.append(abs(band_mean(ruido_b_esp, lo, hi) - verdad))
        temblor_pt.append(abs(point_sample(ruido_b_esp, anchor_q16(b)) - punto))

    err_medio = sum(ganancia_energia) / len(ganancia_energia)
    ti = sum(temblor_int) / len(temblor_int)
    tp = sum(temblor_pt) / len(temblor_pt)
    if err_medio < 0.05:
        raise SystemExit('el muestreo puntual no se desvia: la integracion no aporta')
    if ti >= tp:
        raise SystemExit(f'la integracion no estabiliza la lectura: {ti} >= {tp}')

    anchas = sum(1 for b in range(BANDS) if edge_q16(b + 1) - edge_q16(b) > 65536)
    cubiertos = (edge_q16(BANDS) - edge_q16(0)) / 65536
    print(f'spectral instrument oracle OK: {BANDS} anclas y {BANDS + 1} bordes '
          f'reconstruidos desde la ley, log2 exacto a {peor:.3f} LSB, '
          f'{len(espectros)} espectros integrados byte a byte')
    print(f'  cobertura {cubiertos:.1f} bins; {anchas} bandas mas anchas que un bin')
    print(f'  el muestreo puntual se desviaba {err_medio * 100:.1f}% de la media real '
          f'y temblaba {tp / ti:.2f}x mas entre ventanas vecinas')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
