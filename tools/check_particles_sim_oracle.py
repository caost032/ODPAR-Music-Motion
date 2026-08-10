#!/usr/bin/env python3
"""Oraculo independiente de la simulacion de particulas.

Reconstruye la integracion COMPLETA en Python -- colocacion por semilla, fuerza
de la naturaleza, rozamiento, aleteo, colision contra el nucleo y reciclado por
los bordes -- a partir de la especificacion escrita en `include/odm_particles.h`,
no del codigo de `src/`. El modulo en C se observa unicamente por los enteros
que publica.

Ademas comprueba tres propiedades que no son aritmetica sino contrato:

  1. Ninguna particula queda DENTRO del nucleo despues de un paso. Si el nucleo
     tiene colision, no puede haber polvo dentro de el.
  2. El impulso del choque es proporcional al CRECIMIENTO del nucleo: un nucleo
     grande pero quieto sostiene las particulas fuera sin lanzarlas.
  3. Con caudal, aleteo e impulso a cero el campo no se mueve. Es lo que hace
     que el aire sea una decision explicita del diseno y no un comportamiento
     que el motor aplica por su cuenta.
"""
from __future__ import annotations
import argparse, hashlib, math, pathlib, struct, subprocess, tempfile

Q = 2147483647
MASK64 = (1 << 64) - 1
MAXP = 256

PROBE = r'''#include "odm_particles.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    odm_particles_config cfg;
    odm_particles_state st;
    uint8_t pol[ODM_PARTICLES_POLICY_BYTES];
    uint64_t req = 0u;
    uint32_t nat, paso, i;
    (void)argv;
    if (argc != 1) return 2;
    for (nat = 0u; nat < ODM_PARTICLE_NATURE_COUNT; ++nat) {
        memset(&cfg, 0, sizeof(cfg));
        cfg.schema_version = ODM_PARTICLES_SCHEMA_VERSION;
        cfg.count = 24u;
        cfg.nature = nat;
        cfg.flow_q31 = (uint32_t)(INT32_MAX / 5);
        cfg.drag_q31 = (uint32_t)((INT32_MAX / 100) * 88);
        cfg.impulse_q31 = (uint32_t)(INT32_MAX / 2);
        cfg.flutter_q31 = (uint32_t)((INT32_MAX / 10) * 4);
        cfg.seed = UINT64_C(0x0d130d130d130d13);
        if (odm_particles_init(&cfg, 320u, 180u, &st) != ODM_STATUS_OK) return 3;
        for (paso = 0u; paso < 40u; ++paso) {
            /* Nucleo que respira: crece durante 20 pasos y se queda quieto. */
            int32_t radio = (int32_t)((paso < 20u ? paso : 20u) * 65536u * 2u);
            int32_t crece = paso < 20u ? (int32_t)(65536u * 2u) : 0;
            if (odm_particles_step(&st, &cfg, 160 << 16, 90 << 16, radio, crece)
                != ODM_STATUS_OK) return 4;
        }
        printf("S,%u", nat);
        for (i = 0u; i < st.count; ++i)
            printf(",%" PRId32 ":%" PRId32 ":%" PRId32 ":%" PRId32,
                   st.x_q16[i], st.y_q16[i], st.vx_q16[i], st.vy_q16[i]);
        putchar('\n');
    }
    /* Campo quieto: sin caudal, sin aleteo y sin impulso. */
    memset(&cfg, 0, sizeof(cfg));
    cfg.schema_version = ODM_PARTICLES_SCHEMA_VERSION;
    cfg.count = 16u;
    cfg.nature = ODM_PARTICLE_NATURE_WIND_RIGHT;
    cfg.drag_q31 = 0u;
    cfg.seed = UINT64_C(0x5eed);
    if (odm_particles_init(&cfg, 320u, 180u, &st) != ODM_STATUS_OK) return 5;
    {
        int32_t x0[ODM_PARTICLES_MAX], y0[ODM_PARTICLES_MAX];
        memcpy(x0, st.x_q16, sizeof(x0));
        memcpy(y0, st.y_q16, sizeof(y0));
        for (paso = 0u; paso < 25u; ++paso)
            if (odm_particles_step(&st, &cfg, 0, 0, 0, 0) != ODM_STATUS_OK) return 6;
        printf("Z");
        for (i = 0u; i < st.count; ++i)
            printf(",%d", (st.x_q16[i] == x0[i] && st.y_q16[i] == y0[i]) ? 1 : 0);
        putchar('\n');
    }
    /* Impulso proporcional al CRECIMIENTO, observado en el motor. Dos campos
     * identicos, el mismo radio, y la unica diferencia es que uno crece. */
    {
        uint32_t v;
        for (v = 0u; v < 2u; ++v) {
            int64_t energia = 0;
            memset(&cfg, 0, sizeof(cfg));
            cfg.schema_version = ODM_PARTICLES_SCHEMA_VERSION;
            cfg.count = 8u;
            cfg.nature = ODM_PARTICLE_NATURE_FLOAT;
            cfg.drag_q31 = (uint32_t)((INT32_MAX / 100) * 88);
            cfg.impulse_q31 = (uint32_t)(INT32_MAX / 2);
            cfg.seed = UINT64_C(0x1234);
            if (odm_particles_init(&cfg, 320u, 180u, &st) != ODM_STATUS_OK) return 8;
            if (odm_particles_step(&st, &cfg, 160 << 16, 90 << 16, 512 << 16,
                                   v == 0u ? 0 : (4 << 16)) != ODM_STATUS_OK) return 9;
            for (i = 0u; i < st.count; ++i)
                energia += (st.vx_q16[i] < 0 ? -st.vx_q16[i] : st.vx_q16[i]) +
                           (st.vy_q16[i] < 0 ? -st.vy_q16[i] : st.vy_q16[i]);
            printf("E,%u,%" PRId64 "\n", v, energia);
        }
    }
    if (odm_particles_policy_bytes(pol, sizeof(pol), &req) != ODM_STATUS_OK ||
        req != ODM_PARTICLES_POLICY_BYTES) return 7;
    printf("P,");
    for (i = 0u; i < ODM_PARTICLES_POLICY_BYTES; ++i) printf("%02x", pol[i]);
    putchar('\n');
    printf("K,%u,%u,%u\n", ODM_PARTICLES_SCHEMA_VERSION, ODM_PARTICLES_MAX,
           ODM_PARTICLE_NATURE_COUNT);
    return 0;
}
'''


def i32(v: int) -> int:
    """Saturacion explicita a 32 bits con signo, como hace `pa_clamp_i32`."""
    return max(-2147483648, min(2147483647, v))


def cdiv(n: int, d: int) -> int:
    """Division entera de C: trunca hacia cero."""
    if d == 0:
        return 0
    return n // d if (n >= 0) == (d > 0) else -((abs(n)) // abs(d))


def mix64(x: int) -> int:
    x &= MASK64
    x ^= x >> 33
    x = (x * 0xFF51AFD7ED558CCD) & MASK64
    x ^= x >> 33
    x = (x * 0xC4CEB9FE1A85EC53) & MASK64
    x ^= x >> 33
    return x


def wave_q31(phase: int) -> int:
    """Onda suave por tramos cuadraticos: y = 4x(1-|x|) en Q31."""
    x = (phase & 0xFFFFFFFF) - 0x80000000
    a = abs(x)
    y = cdiv(x * (0x80000000 - a), 1 << 29)
    return max(-Q, min(Q, y))


def nature_force(nature: int, flow: int, h: int, x: int, y: int, cx: int, cy: int):
    f = cdiv(flow * 4096, Q)
    j = ((h >> 40) & 0xFF) - 128
    vx = vy = 0
    if nature == 1:      # viento a la derecha
        vx, vy = f, cdiv(f * j, 512)
    elif nature == 2:    # viento a la izquierda
        vx, vy = -f, cdiv(f * j, 512)
    elif nature == 3:    # caida
        vy, vx = f, cdiv(f * j, 384)
    elif nature == 4:    # ascenso
        vy, vx = -f, cdiv(f * j, 384)
    elif nature == 5:    # remolino
        dx, dy = x - cx, y - cy
        d = math.isqrt((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)) << 8
        if d > 0:
            vx, vy = cdiv(-dy * f, d), cdiv(dx * f, d)
    else:                # flotacion
        vx = cdiv(f * j, 256)
        vy = cdiv(f * (((h >> 48) & 0xFF) - 128), 256)
    return i32(vx), i32(vy)


class Campo:
    def __init__(self, cfg, width, height):
        self.cfg = cfg
        self.w, self.h = width, height
        self.tick = 0
        self.x, self.y, self.vx, self.vy = [], [], [], []
        for i in range(cfg['count']):
            hh = mix64(cfg['seed'] ^ ((i * 0x9E3779B97F4A7C15) & MASK64))
            h2 = mix64(hh)
            self.x.append(((hh & 0xFFFFFFFF) * width) >> 16)
            self.y.append(((h2 & 0xFFFFFFFF) * height) >> 16)
            self.vx.append(cdiv(((hh >> 32) & 0xFFFF) - 32768, 512))
            self.vy.append(cdiv(((h2 >> 32) & 0xFFFF) - 32768, 512))

    def step(self, cx, cy, radio, crece):
        cfg = self.cfg
        wq, hq = self.w << 16, self.h << 16
        if crece < 0:
            crece = 0
        for i in range(cfg['count']):
            hh = mix64(cfg['seed'] ^ ((i * 0x9E3779B97F4A7C15) & MASK64))
            fx, fy = nature_force(cfg['nature'], cfg['flow'], hh,
                                  self.x[i], self.y[i], cx, cy)
            vx = cdiv(self.vx[i] * cfg['drag'], Q) + fx
            vy = cdiv(self.vy[i] * cfg['drag'], Q) + fy
            if cfg['flutter'] != 0:
                paso = (0x02000000 + ((hh >> 24) & 0x01FFFFFF)) & 0xFFFFFFFF
                fase = ((hh >> 16) + self.tick * paso) & 0xFFFFFFFF
                onda = wave_q31(fase)
                amp = cdiv(cfg['flutter'] * 3072, Q)
                m = math.isqrt((vx >> 8) * (vx >> 8) + (vy >> 8) * (vy >> 8)) << 8
                if m > 0:
                    px, py = cdiv(-vy * Q, m), cdiv(vx * Q, m)
                else:
                    px, py = 0, Q
                vx += cdiv(px * cdiv(onda * amp, Q), Q)
                vy += cdiv(py * cdiv(onda * amp, Q), Q)
            x = self.x[i] + vx
            y = self.y[i] + vy
            dx, dy = x - cx, y - cy
            d = math.isqrt((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)) << 8
            if radio > 0 and d < radio:
                if d <= 0:
                    nx, ny, d = Q, 0, 1
                else:
                    nx, ny = cdiv(dx * Q, d), cdiv(dy * Q, d)
                x = cx + cdiv(nx * radio, Q)
                y = cy + cdiv(ny * radio, Q)
                kick = cdiv(crece * cfg['impulse'], Q)
                vx, vy = cdiv(nx * kick, Q), cdiv(ny * kick, Q)
            while x < 0:
                x += wq
            while x >= wq:
                x -= wq
            while y < 0:
                y += hq
            while y >= hq:
                y -= hq
            self.x[i], self.y[i] = i32(x), i32(y)
            self.vx[i], self.vy[i] = i32(vx), i32(vy)
        self.tick += 1


def policy_bytes() -> bytes:
    b = bytearray(b'ODMPART1')
    for v in (1, MAXP, 6, 100, 1, 1, 1, 1, 1, 3072, 4096, 1):
        b.extend(struct.pack('<I', v))
    if len(b) > 256:
        raise AssertionError(len(b))
    b.extend(b'\0' * (256 - len(b)))
    return bytes(b)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--cc', default='gcc')
    ap.add_argument('--library', required=True)
    a = ap.parse_args()
    root = pathlib.Path(a.root).resolve()
    lib = pathlib.Path(a.library).resolve()
    with tempfile.TemporaryDirectory(prefix='odm-particles-sim-') as td_s:
        td = pathlib.Path(td_s)
        src, exe = td / 'p.c', td / 'p'
        src.write_text(PROBE + '\n')
        cmd = [a.cc, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
               '-I', str(root / 'include'), str(src), str(lib),
               '-pthread', '-lpng16', '-lssl', '-lcrypto', '-o', str(exe)]
        r = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit('sonda de particulas: fallo de compilacion\n' + r.stdout + r.stderr)
        r = subprocess.run([str(exe)], cwd=root, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f'sonda de particulas: rc={r.returncode}\n{r.stdout}\n{r.stderr}')
        filas = [ln.split(',') for ln in r.stdout.strip().splitlines()]

    clave = next(f for f in filas if f[0] == 'K')
    if (int(clave[1]), int(clave[2]), int(clave[3])) != (1, MAXP, 6):
        raise SystemExit(f'identidad del modulo distinta: {clave[1:]}')

    pol = next(f for f in filas if f[0] == 'P')[1]
    want = policy_bytes()
    if bytes.fromhex(pol) != want:
        i = next(i for i in range(len(want)) if bytes.fromhex(pol)[i] != want[i])
        raise SystemExit(f'bytes de politica distintos en el desplazamiento {i}')

    cx, cy = 160 << 16, 90 << 16
    revisadas = 0
    dentro = 0
    for fila in [f for f in filas if f[0] == 'S']:
        nat = int(fila[1])
        cfg = {'count': 24, 'nature': nat, 'flow': Q // 5,
               'drag': (Q // 100) * 88, 'impulse': Q // 2,
               'flutter': (Q // 10) * 4, 'seed': 0x0D130D130D130D13}
        campo = Campo(cfg, 320, 180)
        radio_final = 0
        for paso in range(40):
            radio = (paso if paso < 20 else 20) * 65536 * 2
            crece = 65536 * 2 if paso < 20 else 0
            campo.step(cx, cy, radio, crece)
            radio_final = radio
        for k, celda in enumerate(fila[2:]):
            gx, gy, gvx, gvy = (int(v) for v in celda.split(':'))
            if (gx, gy, gvx, gvy) != (campo.x[k], campo.y[k], campo.vx[k], campo.vy[k]):
                raise SystemExit(
                    f'naturaleza {nat} particula {k}: motor=({gx},{gy},{gvx},{gvy}) '
                    f'esperado=({campo.x[k]},{campo.y[k]},{campo.vx[k]},{campo.vy[k]})')
            # 1. Nadie dentro del nucleo.
            dx, dy = gx - cx, gy - cy
            d = math.isqrt((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8)) << 8
            if d < radio_final:
                dentro += 1
            revisadas += 1
    if dentro:
        raise SystemExit(f'{dentro} particulas quedaron DENTRO del nucleo')

    quieto = next(f for f in filas if f[0] == 'Z')
    if any(v != '1' for v in quieto[1:]):
        raise SystemExit('sin caudal, aleteo ni impulso el campo se movio solo')

    # 2. El impulso sigue al CRECIMIENTO, no al tamano. Es el MOTOR quien lo
    #    responde: dos campos identicos, el mismo radio alcanzando a todas las
    #    particulas, y la unica diferencia es que uno crece. Un nucleo quieto
    #    las sostiene fuera sin lanzarlas, asi que su energia tiene que ser
    #    exactamente cero.
    energias = {int(f[1]): int(f[2]) for f in filas if f[0] == 'E'}
    if energias.get(0) != 0:
        raise SystemExit(f'un nucleo quieto lanzo las particulas: {energias.get(0)}')
    if energias.get(1, 0) <= 0:
        raise SystemExit('un nucleo que crece no empujo las particulas')
    base = {'count': 8, 'nature': 0, 'flow': 0, 'drag': (Q // 100) * 88,
            'impulse': Q // 2, 'flutter': 0, 'seed': 0x1234}
    creciendo = Campo(base, 320, 180)
    creciendo.step(cx, cy, 512 << 16, 4 << 16)
    esperado = sum(abs(v) for v in creciendo.vx + creciendo.vy)
    if energias[1] != esperado:
        raise SystemExit(f'impulso del choque: motor={energias[1]} esperado={esperado}')

    firma = hashlib.sha256(
        b''.join(f[1].encode() for f in filas if f[0] == 'P')).hexdigest()
    print('ORACULO DE LA SIMULACION DE PARTICULAS: PASS')
    print(f'  {revisadas} particulas reconstruidas paso a paso sobre 6 naturalezas')
    print('  colision: ninguna particula dentro del nucleo tras 40 pasos')
    print('  impulso proporcional al crecimiento, no al radio')
    print('  caudal/aleteo/impulso a cero: el campo no se mueve solo')
    print(f'  politica sha256={firma}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
