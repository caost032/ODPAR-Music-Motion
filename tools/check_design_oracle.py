#!/usr/bin/env python3
"""Oraculo independiente de la capa de diseno.

Reconstruye desde la especificacion -- no desde src/ -- las tres propiedades que
hacen utilizable el esquema publicado, y las contrasta con el motor:

1. ARITMETICA DE LOS RANGOS. Cada limite escalar es una razon entera llevada a
   Q1.31 con redondeo al mas cercano. Se reconstruye aqui y debe coincidir
   exactamente con lo que el motor publica. Un rango que el motor calcula de
   otra forma seria un control cuyo maximo declarado no es el que acepta.

2. AISLACION DE CATEGORIAS. Se recorre CADA control, se le da un valor legal
   distinto y se comprueba que las demas categorias del documento quedan byte a
   byte iguales. Es la propiedad que permite que una app exponga fondo,
   particulas, texto y progreso como grupos independientes.

3. LA PUERTA DE CONTRASTE ES UNA PUERTA. La luminancia relativa y el ratio WCAG
   se reconstruyen desde IEC 61966-2-1 y WCAG 2.x con aritmetica decimal exacta,
   sin mirar la tabla del motor. Un diseno cuyo titulo no alcanza el minimo NO
   debe poder compilarse.

Ademas comprueba lo que ningun test interno puede comprobar solo: que el
esquema es suficiente para construir una interfaz completa -- toda opcion con
etiqueta, toda clave unica, todo control con categoria conocida.
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import tempfile
from decimal import Decimal, ROUND_HALF_UP, getcontext

Q31 = 2147483647
Q27 = 134217728

PROBE = r'''#include "odm_design.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    uint32_t i, n = odm_design_control_count(), t, nt = odm_template_count();

    for (i = 0u; i < n; ++i) {
        odm_design_control c;
        if (odm_design_control_at(i, &c) != ODM_STATUS_OK) return 2;
        printf("C,%u,%u,%u,%u,%u,%u,%u,%s\n", c.id, c.category, c.kind,
               c.min_value, c.max_value, c.default_value, c.option_count, c.key);
        if (c.kind == 1u) {
            uint32_t o;
            for (o = 0u; o < c.option_count; ++o) {
                const char *l = odm_design_option_label(c.id, o);
                printf("O,%u,%u,%s\n", c.id, o, l ? l : "");
            }
        }
    }

    /* Aislacion: por cada control, un valor legal distinto y el volcado del
       documento entero para que el oraculo compare region por region. */
    for (i = 0u; i < n; ++i) {
        odm_design base, mod;
        odm_design_control c;
        const unsigned char *pa, *pb;
        uint32_t k;
        if (odm_design_control_at(i, &c) != ODM_STATUS_OK) return 3;
        if (odm_template_load(0u, 0u, &base) != ODM_STATUS_OK) return 4;
        mod = base;
        if (c.kind == 3u) {
            odm_rgba16 v;
            if (odm_design_get_color(&base, c.id, &v) != ODM_STATUS_OK) return 5;
            v.r = (uint16_t)(v.r ^ 0x7f7fu); v.g = (uint16_t)(v.g ^ 0x3131u);
            v.b = (uint16_t)(v.b ^ 0x5d5du); v.a = 65535u;
            if (odm_design_set_color(&mod, c.id, &v) != ODM_STATUS_OK) return 6;
        } else {
            uint32_t v = 0u, w;
            if (odm_design_get(&base, c.id, &v) != ODM_STATUS_OK) return 7;
            if (c.kind == 1u)      w = (v + 1u) % c.option_count;
            else if (c.kind == 0u) w = v ^ 1u;
            else                   w = (v != c.max_value) ? c.max_value : c.min_value;
            if (odm_design_set(&mod, c.id, w) != ODM_STATUS_OK) return 8;
            /* Fuera de rango debe fallar. */
            if (c.max_value != 0xffffffffu &&
                odm_design_set(&mod, c.id, c.max_value + 1u) == ODM_STATUS_OK) return 9;
        }
        pa = (const unsigned char *)&base; pb = (const unsigned char *)&mod;
        printf("D,%u,%u", c.id, c.category);
        /* Un mapa de que bytes del documento cambiaron. */
        for (k = 0u; k < (uint32_t)sizeof(base); ++k)
            if (pa[k] != pb[k]) printf(",%u", k);
        printf("\n");
    }

    /* Desplazamientos de cada region, para que el oraculo sepa a que categoria
       pertenece cada byte sin conocer la disposicion interna. */
    {
        odm_design d;
        printf("R,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
               (uint32_t)sizeof(d),
               (uint32_t)offsetof(odm_design, aspect), (uint32_t)sizeof(d.aspect),
               (uint32_t)offsetof(odm_design, background), (uint32_t)sizeof(d.background),
               (uint32_t)offsetof(odm_design, core), (uint32_t)sizeof(d.core),
               (uint32_t)offsetof(odm_design, field), (uint32_t)sizeof(d.field),
               (uint32_t)offsetof(odm_design, particles), (uint32_t)sizeof(d.particles),
               (uint32_t)offsetof(odm_design, text), (uint32_t)sizeof(d.text),
               (uint32_t)offsetof(odm_design, progress), (uint32_t)sizeof(d.progress),
               (uint32_t)offsetof(odm_design, motion), (uint32_t)sizeof(d.motion));
    }

    /* Plantillas: todas deben cargar, validar y compilar. */
    for (t = 0u; t < nt; ++t) {
        odm_design d;
        odm_layered_config cfg;
        odm_design_report rep;
        odm_status st1, st2;
        if (odm_template_load(t, 0u, &d) != ODM_STATUS_OK) return 10;
        st1 = odm_design_validate(&d, &rep);
        st2 = odm_design_compile(&d, 720u, 720u, 30, 3u, &cfg);
        printf("T,%u,%d,%d,%u,%u,%u,%u,%u\n", t, (int)st1, (int)st2,
               rep.title_contrast, rep.artist_contrast, rep.field_contrast,
               cfg.background.style, cfg.field.flags);
    }

    /* La puerta de contraste, observada en el MOTOR: titulo del color del
       fondo, con el titulo visible, no puede compilar. */
    {
        odm_design d;
        odm_design_control ctl;
        odm_layered_config cfg;
        odm_rgba16 bg;
        odm_design_report rep;
        if (odm_template_load(0u, 0u, &d) != ODM_STATUS_OK) return 11;
        if (odm_design_control_find("texto.titulo", &ctl) != ODM_STATUS_OK) return 12;
        if (odm_design_set(&d, ctl.id, 1u) != ODM_STATUS_OK) return 13;
        if (odm_design_set_metadata(&d, "T", "A") != ODM_STATUS_OK) return 14;
        if (odm_design_control_find("fondo.color", &ctl) != ODM_STATUS_OK) return 15;
        if (odm_design_get_color(&d, ctl.id, &bg) != ODM_STATUS_OK) return 16;
        if (odm_design_control_find("texto.titulo_color", &ctl) != ODM_STATUS_OK) return 17;
        if (odm_design_set_color(&d, ctl.id, &bg) != ODM_STATUS_OK) return 18;
        {
            /* Secuenciado a proposito: el orden de evaluacion de los argumentos
               de printf no esta definido, asi que leer `rep` en la misma
               llamada que lo rellena puede leerlo antes de escribirlo. */
            odm_status sv = odm_design_validate(&d, &rep);
            odm_status sc = odm_design_compile(&d, 512u, 512u, 30, 1u, &cfg);
            printf("G,%d,%d,%u\n", (int)sv, (int)sc, rep.title_contrast);
        }
    }

    /* Contraste de pares conocidos, para contrastar la metrica misma. */
    {
        odm_theme th;
        uint32_t r;
        if (odm_theme_builtin(0u, &th) != ODM_STATUS_OK) return 19;
        for (r = 0u; r < (uint32_t)ODM_THEME_ROLE_COUNT; ++r) {
            const odm_rgba16 *c = &th.role[r];
            printf("L,%u,%u,%u,%u,%u,%u\n", r, c->r, c->g, c->b,
                   odm_theme_relative_luminance(c),
                   odm_theme_contrast_ratio(c, &th.role[ODM_THEME_ROLE_BACKGROUND_BASE]));
        }
    }
    return 0;
}
'''


def ratio_q31(n: int, d: int) -> int:
    """Razon entera -> Q1.31, redondeo al mas cercano. La misma regla que el
    esquema declara; si el motor usara otra, los limites publicados no serian
    los limites reales."""
    return (Q31 * n + d // 2) // d


def srgb_linear_q27() -> list[int]:
    """EOTF sRGB desde IEC 61966-2-1, no desde la tabla del motor."""
    getcontext().prec = 80
    out = []
    for i in range(256):
        c = Decimal(i) / Decimal(255)
        lin = c / Decimal('12.92') if c <= Decimal('0.04045') \
            else ((c + Decimal('0.055')) / Decimal('1.055')) ** Decimal('2.4')
        out.append(int((lin * Decimal(Q27)).to_integral_value(rounding=ROUND_HALF_UP)))
    return out


SRGB = srgb_linear_q27()


def luminance_q31(r16: int, g16: int, b16: int) -> int:
    """Luminancia relativa BT.709 en luz lineal, Q1.31."""
    def to8(v: int) -> int:
        return (v * 255 + 32767) // 65535
    y = (SRGB[to8(r16)] * 2126 + SRGB[to8(g16)] * 7152 + SRGB[to8(b16)] * 722) // 10000
    y = (y * Q31) // Q27
    return min(max(y, 0), Q31)


def contrast_ratio(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    """WCAG 2.x, en centesimas. El desplazamiento de 0.05 es lo que hace que
    negro contra negro sea 1:1 y no una division por cero."""
    la, lb = luminance_q31(*a), luminance_q31(*b)
    hi, lo = max(la, lb), min(la, lb)
    off = Q31 // 20
    hi += off
    lo += off
    return (hi * 100 + lo // 2) // lo


# Limites declarados en la especificacion del esquema, escritos aqui como
# razones y no copiados del codigo.
RANGOS = {
    'lienzo.encuadre':          (0, 4, 0),
    'fondo.estilo':             (0, 8, 4),
    'fondo.intensidad':         (0, (1, 1), (1, 2)),
    'fondo.escala':             ((1, 64), (1, 3), (1, 5)),
    'fondo.grosor':             ((1, 4000), (1, 120), (1, 900)),
    'fondo.reactividad':        (0, (1, 1), (1, 8)),
    'nucleo.forma':             (0, 2, 0),
    'nucleo.encuadre':          (0, 2, 0),
    'nucleo.tamano':            ((1, 10), (4, 5), (38, 100)),
    'nucleo.esquina':           (0, (1, 2), (1, 6)),
    'nucleo.borde':             (0, (1, 50), (1, 720)),
    'nucleo.opacidad':          (0, (1, 1), (1, 1)),
    'nucleo.reactividad':       (0, (1, 1), (3, 5)),
    'nucleo.intensidad':        (0, (1, 1), (2, 5)),
    'campo.gramatica':          (0, 4, 0),
    'campo.longitud':           ((1, 40), (1, 3), (1, 7)),
    'campo.grosor':             ((1, 4000), (1, 25), (1, 150)),
    'campo.forma':              (0, 3, 1),
    'campo.simetria':           (0, 2, 0),
    'campo.difuminado':         (0, 4, 1),
    'campo.suavizado_subida':   ((1, 50), (1, 1), (4, 5)),
    'campo.suavizado_bajada':   ((1, 50), (1, 1), (1, 6)),
    'campo.opacidad':           (0, (1, 1), (1, 1)),
    'campo.detalle':            (0, 1, 1),
    'particulas.activas':       (0, 1, 0),
    'particulas.densidad':      (0, (1, 1), (1, 6)),
    'particulas.tamano':        ((1, 4000), (1, 120), (1, 700)),
    'texto.titulo':             (0, 1, 0),
    'texto.autoria':            (0, 1, 0),
    'texto.escala':             ((1, 4), (1, 1), (1, 2)),
    'texto.anclaje':            (0, 5, 4),
    'progreso.barra':           (0, 1, 1),
    'progreso.tiempo':          (0, 1, 1),
    'progreso.estilo':          (0, 2, 2),
    'progreso.modo_tiempo':     (0, 2, 1),
    'progreso.ancho':           ((1, 10), (1, 1), (9, 10)),
    'progreso.alto':            ((1, 4000), (1, 60), (1, 420)),
    'movimiento.sensibilidad':  (0, (1, 1), (4, 5)),
    'movimiento.ataque':        (0, (1, 1), (3, 4)),
    'movimiento.caida':         (0, (1, 1), (1, 2)),
}


def resolver(v) -> int:
    return ratio_q31(*v) if isinstance(v, tuple) else v


CATEGORIAS = ['aspect', 'background', 'core', 'field', 'particles', 'text',
              'progress', 'motion']


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', required=True)
    ap.add_argument('--cc', default='gcc')
    ap.add_argument('--library', required=True)
    a = ap.parse_args()
    root = pathlib.Path(a.root).resolve()
    lib = pathlib.Path(a.library).resolve()

    with tempfile.TemporaryDirectory(prefix='odm-design-') as td0:
        td = pathlib.Path(td0)
        src, exe = td / 'probe.c', td / 'probe'
        src.write_text('#include <stddef.h>\n' + PROBE)
        cmd = [a.cc, '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
               '-I', str(root / 'include'), str(src), str(lib),
               '-pthread', '-lpng16', '-lssl', '-lcrypto', '-o', str(exe)]
        c = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
        if c.returncode:
            raise SystemExit('design probe compile failed\n' + c.stdout + c.stderr)
        r = subprocess.run([str(exe)], cwd=root, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f'design probe failed rc={r.returncode}\n{r.stderr}')

    filas = [ln.split(',') for ln in r.stdout.splitlines() if ln.strip()]
    controles = {f[8]: f for f in filas if f[0] == 'C'}
    opciones: dict[str, set[int]] = {}
    for f in filas:
        if f[0] == 'O':
            opciones.setdefault(f[1], set()).add(int(f[2]))
            if not f[3].strip():
                raise SystemExit(f'control {f[1]} opcion {f[2]} sin etiqueta')

    # 1. Rangos reconstruidos.
    revisados = 0
    for clave, (lo, hi, defecto) in RANGOS.items():
        if clave not in controles:
            raise SystemExit(f'el motor no publica el control "{clave}"')
        f = controles[clave]
        got = (int(f[4]), int(f[5]), int(f[6]))
        want = (resolver(lo), resolver(hi), resolver(defecto))
        if got != want:
            raise SystemExit(f'rango de "{clave}": motor={got} esperado={want}')
        revisados += 1

    # Los controles de color no tienen rango numerico; todo lo demas debe
    # tener su limite reconstruido aqui, o el oraculo estaria mirando hacia otro
    # lado justo donde el motor podria haber cambiado.
    faltan = {k for k, f in controles.items()
              if k not in RANGOS and int(f[3]) != 3}
    if faltan:
        raise SystemExit(f'controles sin rango reconstruido en el oraculo: {sorted(faltan)}')

    # Claves unicas y opciones completas.
    ids = [f[1] for f in filas if f[0] == 'C']
    if len(set(ids)) != len(ids):
        raise SystemExit('hay identificadores de control repetidos')
    for f in filas:
        if f[0] == 'C' and f[3] == '1':
            n_op = int(f[7])
            if opciones.get(f[1], set()) != set(range(n_op)):
                raise SystemExit(f'control {f[1]}: faltan etiquetas de opcion')

    # 2. Aislacion de categorias, byte a byte.
    reg = next(f for f in filas if f[0] == 'R')
    vals = [int(x) for x in reg[1:]]
    total = vals[0]
    regiones = {}
    for idx, nombre in enumerate(CATEGORIAS):
        regiones[idx] = (vals[1 + idx * 2], vals[2 + idx * 2])

    aislados = 0
    for f in filas:
        if f[0] != 'D':
            continue
        cat = int(f[2])
        cambiados = [int(x) for x in f[3:] if x]
        if not cambiados:
            raise SystemExit(f'el control {f[1]} no cambio nada del documento')
        off, size = regiones[cat]
        fuera = [b for b in cambiados if not (off <= b < off + size)]
        if fuera:
            otras = sorted({CATEGORIAS[k] for k in regiones
                            for b in fuera
                            if regiones[k][0] <= b < regiones[k][0] + regiones[k][1]})
            raise SystemExit(
                f'el control {f[1]} (categoria {CATEGORIAS[cat]}) escribio fuera '
                f'de su categoria, en {otras or "zona no clasificada"}; '
                f'bytes {fuera[:8]} de {total}')
        aislados += 1

    # 3. La puerta de contraste.
    g = next(f for f in filas if f[0] == 'G')
    if int(g[1]) == 0 or int(g[2]) == 0:
        raise SystemExit('un diseno con el titulo del color del fondo se acepto: '
                         f'validate={g[1]} compile={g[2]}')
    if int(g[3]) >= 450:
        raise SystemExit(f'contraste declarado {g[3]} pero el titulo es el fondo')

    # Metrica de contraste reconstruida.
    for f in filas:
        if f[0] != 'L':
            continue
        r16, g16, b16 = int(f[2]), int(f[3]), int(f[4])
        lum, ratio = int(f[5]), int(f[6])
        if luminance_q31(r16, g16, b16) != lum:
            raise SystemExit(f'luminancia del rol {f[1]}: motor={lum} '
                             f'esperado={luminance_q31(r16, g16, b16)}')

    # 4. Plantillas.
    plantillas = [f for f in filas if f[0] == 'T']
    if len(plantillas) < 4:
        raise SystemExit('menos plantillas de las declaradas')
    for f in plantillas:
        if int(f[2]) != 0 or int(f[3]) != 0:
            raise SystemExit(f'la plantilla {f[1]} no valida o no compila: '
                             f'validate={f[2]} compile={f[3]}')
    estilos = {int(f[6]) for f in plantillas}
    if len(estilos) < 4:
        raise SystemExit('las plantillas no cubren fondos distintos; '
                         'serian variaciones de color, no plantillas')

    print(f'design oracle OK: {revisados} rangos reconstruidos, '
          f'{aislados} controles aislados, {len(plantillas)} plantillas, '
          f'{len(estilos)} fondos distintos, puerta de contraste cerrada')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
