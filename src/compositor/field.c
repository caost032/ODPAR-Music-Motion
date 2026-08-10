#include "compositor_internal.h"

#include <limits.h>
#include <string.h>
#include <stdint.h>

#define FIELD_CORDIC_K_Q31 INT64_C(1304065748)
#define FIELD_QUARTER_PHASE UINT32_C(0x40000000)

static const uint32_t field_cordic_atan_phase[31] = {
    536870912u,316933406u,167458907u,85004756u,42667331u,21354465u,10679838u,5340245u,
    2670163u,1335087u,667544u,333772u,166886u,83443u,41722u,20861u,10430u,5215u,2608u,
    1304u,652u,326u,163u,81u,41u,20u,10u,5u,3u,1u,1u
};

static int32_t sin_q31(uint32_t phase) {
    uint32_t quadrant = phase >> 30, offset = phase & UINT32_C(0x3fffffff);
    int sign = quadrant >= 2u ? -1 : 1;
    int64_t z = (quadrant == 0u || quadrant == 2u) ? (int64_t)offset :
                (int64_t)(FIELD_QUARTER_PHASE - offset);
    int64_t x = FIELD_CORDIC_K_Q31, y = 0;
    unsigned i;
    if (phase == 0u || phase == UINT32_C(0x80000000)) return 0;
    if (phase == FIELD_QUARTER_PHASE) return INT32_MAX;
    if (phase == UINT32_C(0xc0000000)) return -INT32_MAX;
    for (i=0u;i<31u;++i) {
        int64_t d = INT64_C(1) << i, xs=x/d, ys=y/d, nx, ny;
        if (z >= 0) { nx=x-ys; ny=y+xs; z-=(int64_t)field_cordic_atan_phase[i]; }
        else { nx=x+ys; ny=y-xs; z+=(int64_t)field_cordic_atan_phase[i]; }
        x=nx; y=ny;
    }
    if (sign < 0) y=-y;
    if (y > INT32_MAX) y=INT32_MAX;
    if (y < -INT32_MAX) y=-INT32_MAX;
    return (int32_t)y;
}

static int32_t cos_q31(uint32_t phase) { return sin_q31(phase + FIELD_QUARTER_PHASE); }

static int64_t abs_i64(int64_t v) { return v < 0 ? -v : v; }

static uint32_t field_mul_q31(uint32_t a, uint32_t b) {
    uint64_t product = (uint64_t)a * (uint64_t)b + (uint64_t)INT32_MAX / 2u;
    product /= (uint64_t)INT32_MAX;
    return product > (uint64_t)INT32_MAX ? (uint32_t)INT32_MAX : (uint32_t)product;
}

/* Canonical signed nearest-integer division, ties away from zero.  Geometry
 * must not depend on arithmetic right-shift behavior or on a sign-biased
 * +half rounding shortcut. */
static int64_t div_round_away_i64(int64_t n, int64_t d) {
    if (d <= 0) return 0;
    if (n >= 0) return (n + d / 2) / d;
    return -(((-n) + d / 2) / d);
}

static uint64_t mix64(uint64_t x) {
    x += UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

/* Exact phase advance for sample-authoritative particles.  Computing
 * sample*speed directly can overflow uint64_t near the admitted int64 sample
 * horizon before the division by 480.  Quotient/remainder decomposition is
 * algebraically identical and bounds the only wide product; the final cast is
 * the intentional modulo-2^32 phase domain. */
static uint32_t particle_phase_advance(int64_t sample, uint32_t speed) {
    uint64_t s, whole, frac;
    if (sample <= 0) return 0u;
    s = (uint64_t)sample;
    whole = (uint64_t)(uint32_t)(s / UINT64_C(480)) * (uint64_t)speed;
    frac = ((s % UINT64_C(480)) * (uint64_t)speed) / UINT64_C(480);
    return (uint32_t)(whole + frac);
}


static int32_t core_boundary_radius_q16(const odm_layered_config *c,
                                        const odm_layered_frame_plan *p,
                                        int32_t cs_q31, int32_t sn_q31) {
    int64_t lo = 0;
    uint64_t hw = (uint64_t)(p->core_half_w_q16 < 0 ? 0 : p->core_half_w_q16);
    uint64_t hh = (uint64_t)(p->core_half_h_q16 < 0 ? 0 : p->core_half_h_q16);
    int64_t hi = (int64_t)odm_layered_isqrt_u64(hw * hw + hh * hh) + INT64_C(131072);
    unsigned iter;
    if (c->core.shape == ODM_CORE_SHAPE_CIRCLE) return p->core_radius_q16;
    if (hi <= 0) return 0;
    for (iter = 0u; iter < 28u && hi - lo > 1; ++iter) {
        int64_t mid = lo + (hi - lo) / 2;
        int64_t dx = (mid * cs_q31) / INT32_MAX;
        int64_t dy = (mid * sn_q31) / INT32_MAX;
        int64_t sdf = odm_layered_core_sdf_q16(c, p, dx, dy);
        if (sdf <= 0) lo = mid; else hi = mid;
    }
    return (int32_t)(lo > INT32_MAX ? INT32_MAX : lo);
}

static void draw_disk(odm_layered_pixel16 *frame, uint32_t w, uint32_t h,
                      int32_t cx_q16, int32_t cy_q16, int32_t radius_q16,
                      const odm_rgba16 *color, uint32_t opacity_q31) {
    int64_t r = radius_q16, feather = INT64_C(65536);
    int64_t minx = ((int64_t)cx_q16 - r - feather) >> 16;
    int64_t maxx = ((int64_t)cx_q16 + r + feather) >> 16;
    int64_t miny = ((int64_t)cy_q16 - r - feather) >> 16;
    int64_t maxy = ((int64_t)cy_q16 + r + feather) >> 16;
    int64_t x,y;
    if (radius_q16 <= 0) return;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int64_t)w) maxx = (int64_t)w - 1;
    if (maxy >= (int64_t)h) maxy = (int64_t)h - 1;
    for (y=miny;y<=maxy;++y) for (x=minx;x<=maxx;++x) {
        int64_t dx=((x<<16)+32768)-cx_q16, dy=((y<<16)+32768)-cy_q16;
        uint64_t ux=(uint64_t)abs_i64(dx), uy=(uint64_t)abs_i64(dy);
        int64_t dist=(int64_t)odm_layered_isqrt_u64(ux*ux+uy*uy);
        uint32_t cov;
        int64_t sdf=dist-r;
        if (sdf <= -feather/2) cov=(uint32_t)INT32_MAX;
        else if (sdf >= feather/2) cov=0u;
        else cov=(uint32_t)(((feather/2-sdf)*(int64_t)INT32_MAX+feather/2)/feather);
        if (cov) odm_layered_blend16(&frame[(uint64_t)y*w+(uint64_t)x],color,cov,opacity_q31);
    }
}

/* PARTICULA CON FORMA Y ORIENTACION.
 *
 * Un disco no tiene direccion, asi que un disco nunca puede parecer una pluma
 * ni una hoja por mucho que se mueva. La silueta se evalua en el marco de la
 * particula -- `u` a lo largo de su velocidad, `v` a traves -- y ahi es una
 * elipse cuyo semieje transversal se estrecha hacia las puntas. Eso da una
 * lente, que es la forma que el ojo lee como algo que corta el aire.
 *
 * `along_q16`/`across_q16` son los semiejes en pixeles; `taper_q31` cuanto se
 * estrecha en las puntas (0 = elipse limpia, Q31 = punta). La direccion llega
 * ya normalizada en Q31 para que la forma no dependa de la magnitud, y el
 * antialias es el mismo modelo de un pixel que usa todo el compositor, medido
 * en unidades normalizadas para que no se deforme con la elongacion. */
static void draw_lens(odm_layered_pixel16 *frame, uint32_t w, uint32_t h,
                      int32_t cx_q16, int32_t cy_q16,
                      int32_t along_q16, int32_t across_q16,
                      int32_t dirx_q31, int32_t diry_q31, uint32_t taper_q31,
                      const odm_rgba16 *color, uint32_t opacity_q31) {
    int64_t ext = along_q16 > across_q16 ? along_q16 : across_q16;
    int64_t minx, maxx, miny, maxy, x, y;
    /* Un pixel expresado en unidades normalizadas del eje mas corto: es el
     * ancho real de la transicion en la direccion en que la forma es mas fina. */
    int64_t corto = along_q16 < across_q16 ? along_q16 : across_q16;
    int64_t borde;
    if (along_q16 <= 0 || across_q16 <= 0) return;
    if (corto < 4096) corto = 4096;
    borde = (INT64_C(65536) * INT64_C(65536)) / corto;   /* Q16 normalizado */
    if (borde > 32768) borde = 32768;
    minx = ((int64_t)cx_q16 - ext - 65536) >> 16;
    maxx = ((int64_t)cx_q16 + ext + 65536) >> 16;
    miny = ((int64_t)cy_q16 - ext - 65536) >> 16;
    maxy = ((int64_t)cy_q16 + ext + 65536) >> 16;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int64_t)w) maxx = (int64_t)w - 1;
    if (maxy >= (int64_t)h) maxy = (int64_t)h - 1;
    for (y = miny; y <= maxy; ++y) for (x = minx; x <= maxx; ++x) {
        int64_t dx = ((x << 16) + 32768) - cx_q16;
        int64_t dy = ((y << 16) + 32768) - cy_q16;
        /* Al marco de la particula. */
        int64_t u = (dx * dirx_q31 + dy * diry_q31) / INT32_MAX;
        int64_t v = (-dx * diry_q31 + dy * dirx_q31) / INT32_MAX;
        int64_t un = (u * 65536) / along_q16;            /* Q16 normalizado */
        int64_t vn, ancho, dist;
        uint32_t cov;
        if (un > 65536 || un < -65536) continue;
        /* Estrechamiento hacia las puntas: 1 en el centro, 1-taper en el extremo. */
        ancho = INT64_C(65536) -
                ((un < 0 ? -un : un) * (int64_t)taper_q31) / INT32_MAX;
        if (ancho < 4096) ancho = 4096;
        vn = (v * 65536) / across_q16;
        vn = (vn * 65536) / ancho;
        if (vn > 262144 || vn < -262144) continue;
        dist = (int64_t)odm_layered_isqrt_u64((uint64_t)(un * un + vn * vn));
        if (dist <= 65536 - borde) cov = (uint32_t)INT32_MAX;
        else if (dist >= 65536) cov = 0u;
        else cov = (uint32_t)(((65536 - dist) * (int64_t)INT32_MAX) / borde);
        if (cov) odm_layered_blend16(&frame[(uint64_t)y * w + (uint64_t)x], color, cov, opacity_q31);
    }
}

/* Dibuja una particula segun su forma declarada. La velocidad decide la
 * orientacion -- y solo la orientacion: la forma no cambia de tamano porque
 * corra mas, salvo la estela, que es precisamente lo que una estela significa. */
static void draw_particle(odm_layered_pixel16 *frame, uint32_t w, uint32_t h,
                          int32_t x_q16, int32_t y_q16, int32_t radius_q16,
                          uint32_t shape, int32_t vx_q16, int32_t vy_q16,
                          const odm_rgba16 *color, uint32_t opacity_q31) {
    int64_t speed;
    int32_t dx_q31 = INT32_MAX, dy_q31 = 0;
    if (radius_q16 <= 0) return;
    if (shape == ODM_FIELD_PARTICLE_DUST) {
        draw_disk(frame, w, h, x_q16, y_q16, radius_q16, color, opacity_q31);
        return;
    }
    speed = (int64_t)odm_layered_isqrt_u64(
        (uint64_t)((int64_t)vx_q16 * vx_q16 + (int64_t)vy_q16 * vy_q16));
    /* Una particula practicamente parada no tiene direccion que respetar. Se le
     * da la horizontal para que la silueta sea estable en vez de girar sobre el
     * ruido de una velocidad casi nula, que se leeria como parpadeo. */
    if (speed > 256) {
        dx_q31 = (int32_t)(((int64_t)vx_q16 * INT32_MAX) / speed);
        dy_q31 = (int32_t)(((int64_t)vy_q16 * INT32_MAX) / speed);
    }
    switch (shape) {
        case ODM_FIELD_PARTICLE_FEATHER:
            /* Pluma: larga, muy fina y con las dos puntas afiladas. */
            draw_lens(frame, w, h, x_q16, y_q16,
                      (int32_t)(((int64_t)radius_q16 * 13) / 4),
                      (int32_t)(((int64_t)radius_q16 * 3) / 5),
                      dx_q31, dy_q31, (uint32_t)((INT32_MAX / 10) * 9),
                      color, opacity_q31);
            break;
        case ODM_FIELD_PARTICLE_LEAF:
            /* Hoja: ancha, con cuerpo y punta menos afilada. */
            draw_lens(frame, w, h, x_q16, y_q16,
                      (int32_t)(((int64_t)radius_q16 * 9) / 5),
                      (int32_t)radius_q16,
                      dx_q31, dy_q31, (uint32_t)((INT32_MAX / 5) * 3),
                      color, opacity_q31);
            break;
        case ODM_FIELD_PARTICLE_SPARK: {
            /* Estela: se alarga con lo que se mueve. Es el unico dibujo que
             * depende de la magnitud de la velocidad, porque es lo que una
             * estela ES: el rastro de un desplazamiento. */
            int64_t largo = (int64_t)radius_q16 + speed * 3;
            int64_t techo = (int64_t)radius_q16 * 12;
            if (largo > techo) largo = techo;
            draw_lens(frame, w, h, x_q16, y_q16, (int32_t)largo,
                      (int32_t)(((int64_t)radius_q16 * 2) / 5),
                      dx_q31, dy_q31, (uint32_t)((INT32_MAX / 4) * 3),
                      color, opacity_q31);
            break;
        }
        default: {
            /* Copo: tres agujas cruzadas a 60 grados. Se construye rotando la
             * direccion con la identidad exacta de 60 grados, sin trigonometria
             * en el bucle: (c,s) = (1/2, raiz(3)/2). */
            const int64_t medio = INT32_MAX / 2;
            const int64_t raiz3 = INT64_C(1859775393); /* raiz(3)/2 en Q31 */
            int32_t ax = dx_q31, ay = dy_q31;
            int32_t bx = (int32_t)((ax * medio - (int64_t)ay * raiz3) / INT32_MAX);
            int32_t by = (int32_t)(((int64_t)ax * raiz3 + ay * medio) / INT32_MAX);
            int32_t cx = (int32_t)((ax * medio + (int64_t)ay * raiz3) / INT32_MAX);
            int32_t cy = (int32_t)((-(int64_t)ax * raiz3 + ay * medio) / INT32_MAX);
            int32_t largo = (int32_t)(((int64_t)radius_q16 * 5) / 2);
            int32_t ancho = (int32_t)(((int64_t)radius_q16 * 2) / 5);
            draw_lens(frame, w, h, x_q16, y_q16, largo, ancho, ax, ay, 0u, color, opacity_q31);
            draw_lens(frame, w, h, x_q16, y_q16, largo, ancho, bx, by, 0u, color, opacity_q31);
            draw_lens(frame, w, h, x_q16, y_q16, largo, ancho, cx, cy, 0u, color, opacity_q31);
            break;
        }
    }
}

/* Segment rasterization is canonically Q24.8.  Projection uses Q16 t so all
 * products fit signed 64-bit for the admitted 16k canvas / 2k bar limits. */
/* Mezcla lineal de dos colores. Bajo gobierno espectral directo el color de la
 * aguja sale de SU PROPIA intensidad: apagado en las bandas debiles, acento
 * pleno en las fuertes. Antes el acento solo se encendia con el ataque de
 * Music-Reaction, asi que con la medida directa -- donde ataque y cola valen
 * cero por definicion -- ninguna barra llegaba a encenderse: todas quedaban en
 * el color apagado y en el suelo de opacidad. Se veian flacas y muertas. */
static odm_rgba16 field_mix_color(const odm_rgba16 *a, const odm_rgba16 *b, uint32_t t_q31) {
    odm_rgba16 o;
    uint64_t inv = (uint64_t)INT32_MAX - (uint64_t)t_q31;
    o.r = (uint16_t)(((uint64_t)a->r * inv + (uint64_t)b->r * t_q31) / (uint64_t)INT32_MAX);
    o.g = (uint16_t)(((uint64_t)a->g * inv + (uint64_t)b->g * t_q31) / (uint64_t)INT32_MAX);
    o.b = (uint16_t)(((uint64_t)a->b * inv + (uint64_t)b->b * t_q31) / (uint64_t)INT32_MAX);
    o.a = (uint16_t)(((uint64_t)a->a * inv + (uint64_t)b->a * t_q31) / (uint64_t)INT32_MAX);
    return o;
}

static void draw_segment(odm_layered_pixel16 *frame, uint32_t w, uint32_t h,
                         int32_t ax_q16, int32_t ay_q16,
                         int32_t bx_q16, int32_t by_q16,
                         int32_t width_q16, const odm_rgba16 *color,
                         uint32_t opacity_q31) {
    int64_t ax=div_round_away_i64(ax_q16, INT64_C(256));
    int64_t ay=div_round_away_i64(ay_q16, INT64_C(256));
    int64_t bx=div_round_away_i64(bx_q16, INT64_C(256));
    int64_t by=div_round_away_i64(by_q16, INT64_C(256));
    int64_t vx=bx-ax, vy=by-ay, len2=vx*vx+vy*vy;
    int64_t rad=(int64_t)width_q16/2 + INT64_C(65536);
    int64_t minx=((ax_q16<bx_q16?ax_q16:bx_q16)-rad)>>16;
    int64_t maxx=((ax_q16>bx_q16?ax_q16:bx_q16)+rad)>>16;
    int64_t miny=((ay_q16<by_q16?ay_q16:by_q16)-rad)>>16;
    int64_t maxy=((ay_q16>by_q16?ay_q16:by_q16)+rad)>>16;
    int64_t x,y;
    if (len2 <= 0) { draw_disk(frame,w,h,ax_q16,ay_q16,width_q16/2,color,opacity_q31); return; }
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int64_t)w) maxx = (int64_t)w - 1;
    if (maxy >= (int64_t)h) maxy = (int64_t)h - 1;
    for(y=miny;y<=maxy;++y)for(x=minx;x<=maxx;++x){
        int64_t px=(x<<8)+128, py=(y<<8)+128;
        int64_t dot=(px-ax)*vx+(py-ay)*vy, tq16, cx, cy, dx, dy, dist_q16;
        uint32_t cov;
        if(dot<=0)tq16=0;else if(dot>=len2)tq16=65536;else tq16=(dot*65536+len2/2)/len2;
        cx=ax+div_round_away_i64(vx*tq16, INT64_C(65536));
        cy=ay+div_round_away_i64(vy*tq16, INT64_C(65536));
        dx=px-cx;dy=py-cy;
        dist_q16=(int64_t)odm_layered_isqrt_u64((uint64_t)(dx*dx+dy*dy))<<8;
        cov=odm_layered_coverage_q31(dist_q16,width_q16/2,65536);
        if(cov)odm_layered_blend16(&frame[(uint64_t)y*w+(uint64_t)x],color,cov,opacity_q31);
    }
}

/* Una aguja radial, con forma. La forma no cambia el valor ni la longitud: solo
 * como ocupa el espacio ese mismo valor. Todas se construyen sobre el mismo
 * segmento, asi que ninguna introduce geometria que las demas no puedan
 * reproducir. */
static void draw_needle(odm_layered_pixel16 *frame, uint32_t w, uint32_t h,
                        int32_t ax, int32_t ay, int32_t bx, int32_t by,
                        int32_t width_q16, uint32_t shape,
                        const odm_rgba16 *color, uint32_t opacity_q31) {
    if (opacity_q31 == 0u || width_q16 <= 0) return;
    switch (shape) {
        case ODM_FIELD_BAR_CAPSULE: {
            /* Solo el casquete exterior. El interior nace pegado al borde del
             * nucleo, que lo dibuja encima: pintarlo es trabajo que nadie ve, y
             * son 96 discos por cuadro. */
            draw_segment(frame, w, h, ax, ay, bx, by, width_q16, color, opacity_q31);
            draw_disk(frame, w, h, bx, by, width_q16 / 2, color, opacity_q31);
            break;
        }
        case ODM_FIELD_BAR_WEDGE: {
            /* Cuatro tramos con grosor decreciente: afilada hacia fuera sin
             * necesitar un rasterizador de poligonos aparte. */
            uint32_t k;
            for (k = 0u; k < 4u; ++k) {
                int32_t x0 = ax + (int32_t)(((int64_t)(bx - ax) * (int32_t)k) / 4);
                int32_t y0 = ay + (int32_t)(((int64_t)(by - ay) * (int32_t)k) / 4);
                int32_t x1 = ax + (int32_t)(((int64_t)(bx - ax) * (int32_t)(k + 1u)) / 4);
                int32_t y1 = ay + (int32_t)(((int64_t)(by - ay) * (int32_t)(k + 1u)) / 4);
                int32_t ww = (int32_t)(((int64_t)width_q16 * (int64_t)(4u - k)) / 4);
                if (ww < (1 << 12)) ww = 1 << 12;
                draw_segment(frame, w, h, x0, y0, x1, y1, ww, color, opacity_q31);
            }
            break;
        }
        case ODM_FIELD_BAR_DOTS: {
            /* Columna de puntos separados un diametro y medio. El numero sale
             * de la longitud, asi que una banda mas alta tiene mas puntos en
             * vez de puntos mas grandes. */
            int64_t dx = (int64_t)bx - ax, dy = (int64_t)by - ay;
            int64_t len = (int64_t)odm_layered_isqrt_u64((uint64_t)((dx >> 8) * (dx >> 8) +
                                                                     (dy >> 8) * (dy >> 8))) << 8;
            int64_t paso = (int64_t)width_q16 * 3 / 2;
            int64_t n, i2;
            if (paso <= 0) paso = 1 << 16;
            n = len / paso;
            if (n > 64) n = 64;
            for (i2 = 0; i2 <= n; ++i2) {
                int32_t px = ax + (int32_t)((dx * i2) / (n > 0 ? n : 1));
                int32_t py = ay + (int32_t)((dy * i2) / (n > 0 ? n : 1));
                draw_disk(frame, w, h, px, py, width_q16 / 2, color, opacity_q31);
            }
            break;
        }
        default:
            draw_segment(frame, w, h, ax, ay, bx, by, width_q16, color, opacity_q31);
            break;
    }
}

static void draw_orbit_ring_circle(const odm_layered_config *c,
                                   const odm_layered_frame_plan *p,
                                   odm_layered_pixel16 *frame) {
    int64_t half = (int64_t)c->field.bar_width_q16 / 2;
    int64_t target = (int64_t)p->core_radius_q16 + (int64_t)c->field.ring_gap_q16;
    int64_t outer = target + half + INT64_C(65536);
    int64_t inner = target - half - INT64_C(65536);
    int64_t miny = ((int64_t)p->center_y_q16 - outer) >> 16;
    int64_t maxy = ((int64_t)p->center_y_q16 + outer) >> 16;
    int64_t y;
    if (inner < 0) inner = 0;
    if (miny < 0) miny = 0;
    if (maxy >= (int64_t)p->height) maxy = (int64_t)p->height - 1;
    for (y = miny; y <= maxy; ++y) {
        int64_t py = (y << 16) + INT64_C(32768);
        int64_t dy = py - p->center_y_q16;
        uint64_t ady = (uint64_t)abs_i64(dy);
        uint64_t outer2, dy2, xo;
        int64_t left_lo, left_hi, right_lo, right_hi;
        if (ady >= (uint64_t)outer) continue;
        outer2 = (uint64_t)outer * (uint64_t)outer;
        dy2 = ady * ady;
        xo = odm_layered_isqrt_u64(outer2 - dy2);
        left_lo = ((int64_t)p->center_x_q16 - (int64_t)xo) >> 16;
        right_hi = ((int64_t)p->center_x_q16 + (int64_t)xo) >> 16;
        /* One extra candidate on each outer edge is conservative and exact:
         * final SDF coverage still decides publication. */
        left_lo -= 1;
        right_hi += 1;
        if (left_lo < 0) left_lo = 0;
        if (right_hi >= (int64_t)p->width) right_hi = (int64_t)p->width - 1;
        if (inner == 0 || ady >= (uint64_t)inner) {
            int64_t x;
            for (x = left_lo; x <= right_hi; ++x) {
                int64_t dx = ((x << 16) + INT64_C(32768)) - p->center_x_q16;
                uint64_t ux = (uint64_t)abs_i64(dx);
                int64_t dist = (int64_t)odm_layered_isqrt_u64(ux * ux + dy2);
                uint32_t cov = odm_layered_coverage_q31(abs_i64(dist - target), half, 65536);
                if (cov) odm_layered_blend16(&frame[(uint64_t)y*p->width+(uint64_t)x],
                                             &c->field.secondary_color,cov,p->field_opacity_q31);
            }
        } else {
            uint64_t inner2 = (uint64_t)inner * (uint64_t)inner;
            uint64_t xi = odm_layered_isqrt_u64(inner2 - dy2);
            int64_t x;
            left_hi = ((int64_t)p->center_x_q16 - (int64_t)xi) >> 16;
            right_lo = ((int64_t)p->center_x_q16 + (int64_t)xi) >> 16;
            left_hi += 1;
            right_lo -= 1;
            if (left_hi >= right_lo) {
                for (x = left_lo; x <= right_hi; ++x) {
                    int64_t dx = ((x << 16) + INT64_C(32768)) - p->center_x_q16;
                    uint64_t ux = (uint64_t)abs_i64(dx);
                    int64_t dist = (int64_t)odm_layered_isqrt_u64(ux * ux + dy2);
                    uint32_t cov = odm_layered_coverage_q31(abs_i64(dist - target), half, 65536);
                    if (cov) odm_layered_blend16(&frame[(uint64_t)y*p->width+(uint64_t)x],
                                                 &c->field.secondary_color,cov,p->field_opacity_q31);
                }
            } else {
                if (left_hi > right_hi) left_hi = right_hi;
                for (x = left_lo; x <= left_hi; ++x) {
                    int64_t dx = ((x << 16) + INT64_C(32768)) - p->center_x_q16;
                    uint64_t ux = (uint64_t)abs_i64(dx);
                    int64_t dist = (int64_t)odm_layered_isqrt_u64(ux * ux + dy2);
                    uint32_t cov = odm_layered_coverage_q31(abs_i64(dist - target), half, 65536);
                    if (cov) odm_layered_blend16(&frame[(uint64_t)y*p->width+(uint64_t)x],
                                                 &c->field.secondary_color,cov,p->field_opacity_q31);
                }
                if (right_lo < left_lo) right_lo = left_lo;
                for (x = right_lo; x <= right_hi; ++x) {
                    int64_t dx = ((x << 16) + INT64_C(32768)) - p->center_x_q16;
                    uint64_t ux = (uint64_t)abs_i64(dx);
                    int64_t dist = (int64_t)odm_layered_isqrt_u64(ux * ux + dy2);
                    uint32_t cov = odm_layered_coverage_q31(abs_i64(dist - target), half, 65536);
                    if (cov) odm_layered_blend16(&frame[(uint64_t)y*p->width+(uint64_t)x],
                                                 &c->field.secondary_color,cov,p->field_opacity_q31);
                }
            }
        }
    }
}

/* AUTORIDAD DE UN SECTOR, SIN GEOMETRIA.
 *
 * Esto es lo que hace posible tener varias composiciones sin que la reaccion a
 * la musica cambie. Aqui se decide QUE vale cada sector -- cuanto ocupa el
 * cuerpo sostenido, cuanto la cola, donde acaba la punta del ataque, con que
 * color y con que opacidad -- en fracciones Q1.31, sin saber donde se va a
 * dibujar. La composicion elige despues el punto de anclaje y la direccion.
 *
 * Si esta decision viviera dentro de cada composicion, dos composiciones
 * podrian discrepar sobre lo que la misma musica significa, y bastaria cambiar
 * de diseno para que la cancion se leyera distinto. Vive UNA sola vez. */
typedef struct {
    int provenance;           /* 1 = cuerpo + cola + punta; 0 = una sola aguja */
    uint32_t body_q31;        /* fraccion del alcance maximo                   */
    uint32_t release_q31;
    uint32_t final_q31;
    uint32_t body_op;
    uint32_t release_op;
    uint32_t tip_op;
    odm_rgba16 body_col;
    odm_rgba16 release_col;
    odm_rgba16 tip_col;
    uint32_t simple_op;
    odm_rgba16 simple_col;
} field_authority;

static void field_segment_authority(const odm_layered_config *c,
                                    const odm_layered_frame_plan *p,
                                    uint32_t i, field_authority *a) {
    memset(a, 0, sizeof(*a));
    if ((p->flags & ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE) != 0u) {
        uint32_t release_base_q31 = p->radial_body_q31[i];
        const int directo =
            (p->flags & ODM_COMPOSITION_FLAG_DIRECT_SPECTRAL_INSTRUMENT) != 0u;
        a->provenance = 1;
        a->body_q31 = p->radial_body_q31[i];
        a->final_q31 = p->radial_q31[i];
        if ((p->flags & ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE) != 0u) {
            const uint32_t release_weight_q31 = UINT32_C(751619277); /* 0.35 */
            uint32_t release_mix = field_mul_q31(p->radial_release_q31[i], release_weight_q31);
            release_base_q31 += field_mul_q31((uint32_t)INT32_MAX - release_base_q31, release_mix);
        }
        a->release_q31 = release_base_q31;
        if (directo) {
            /* La medida ES la evidencia: no hay nada que exigirle ademas. Y el
             * color sale de su propia intensidad, de modo que las bandas
             * fuertes se encienden con el acento en vez de quedarse apagadas. */
            a->body_op = field_mul_q31(p->field_opacity_q31, p->radial_q31[i]);
            a->body_col = field_mix_color(&c->field.secondary_color,
                                          &c->field.primary_color, p->radial_q31[i]);
        } else {
            uint32_t local_activity = p->radial_attack_q31[i];
            uint32_t tail_activity = field_mul_q31(p->radial_release_q31[i],
                                                   LAYER_RADIAL_TAIL_WEIGHT);
            uint32_t activity = local_activity > tail_activity ? local_activity : tail_activity;
            uint32_t authority = LAYER_RADIAL_AUTHORITY_FLOOR;
            authority += field_mul_q31((uint32_t)INT32_MAX - authority, activity);
            a->body_op = field_mul_q31(p->field_opacity_q31, authority);
            a->body_col = c->field.secondary_color;
        }
        a->release_op = p->radial_release_q31[i] == 0u ? 0u
                        : field_mul_q31(p->field_opacity_q31, p->radial_release_q31[i]);
        a->release_col = c->field.secondary_color;
        a->tip_op = p->radial_attack_q31[i] == 0u ? 0u
                    : field_mul_q31(p->field_opacity_q31,
                                    field_mul_q31(p->radial_attack_q31[i],
                                                  p->radial_attack_q31[i]));
        a->tip_col = c->field.primary_color;
    } else {
        a->provenance = 0;
        a->final_q31 = p->radial_q31[i];
        a->simple_op = p->field_opacity_q31;
        a->simple_col = (i & 1u) ? c->field.secondary_color : c->field.primary_color;
    }
}

/* Fraccion Q1.31 llevada a una longitud en Q16.16. */
static int64_t field_len_q16(int64_t alcance, uint32_t q31) {
    return (alcance * (int64_t)q31 + INT32_MAX / 2) / INT32_MAX;
}

/* ANCLA DE UNA COMPOSICION.
 *
 * Una composicion solo decide dos cosas: de donde sale cada sector y hacia
 * donde crece. La corona proyecta desde el centro sobre un radio -- y se
 * conserva esa forma exacta de proyectar, no una equivalente, porque cambiar
 * el orden de las divisiones moveria pixeles de un render ya fijado por
 * oraculos. Las demas parten de un punto y avanzan en linea recta. */
typedef struct {
    int radial;
    int32_t cx, cy;        /* centro, solo en corona                        */
    int64_t r0;            /* radio base, solo en corona                    */
    int32_t ax, ay;        /* ancla, en las composiciones rectas            */
    int32_t ux, uy;        /* direccion unitaria en Q1.31                   */
} field_anchor;

static void field_point(const field_anchor *g, int64_t len, int32_t *x, int32_t *y) {
    if (g->radial) {
        int64_t r = g->r0 + len;
        *x = (int32_t)((int64_t)g->cx + (r * g->ux) / INT32_MAX);
        *y = (int32_t)((int64_t)g->cy + (r * g->uy) / INT32_MAX);
    } else {
        *x = (int32_t)((int64_t)g->ax + (len * g->ux) / INT32_MAX);
        *y = (int32_t)((int64_t)g->ay + (len * g->uy) / INT32_MAX);
    }
}

/* Dibuja un sector a lo largo de su ancla. Es la unica funcion que sabe pintar
 * un sector, y no sabe NADA de que composicion lo pidio: por eso una
 * composicion nueva no puede cambiar lo que la musica significa. */
static void field_draw_segment(odm_layered_pixel16 *frame, uint32_t w, uint32_t h,
                               const odm_layered_config *c,
                               const field_authority *a,
                               const field_anchor *g,
                               int64_t alcance_q16, int64_t minimo_q16) {
    int32_t ax, ay, bx, by, rx, ry, tx, ty;
    int64_t body, release, final_len;
    field_point(g, 0, &ax, &ay);
    if (!a->provenance) {
        int64_t len = minimo_q16 + field_len_q16(alcance_q16 - minimo_q16, a->final_q31);
        field_point(g, len, &bx, &by);
        draw_needle(frame, w, h, ax, ay, bx, by, (int32_t)c->field.bar_width_q16,
                    c->field.bar_shape, &a->simple_col, a->simple_op);
        return;
    }
    body = field_len_q16(alcance_q16, a->body_q31);
    release = field_len_q16(alcance_q16, a->release_q31);
    final_len = field_len_q16(alcance_q16, a->final_q31);
    field_point(g, body, &bx, &by);
    rx = bx; ry = by;
    if (body > 0 && a->body_op != 0u)
        draw_needle(frame, w, h, ax, ay, bx, by, (int32_t)c->field.bar_width_q16,
                    c->field.bar_shape, &a->body_col, a->body_op);
    if (release > body && a->release_op != 0u) {
        field_point(g, release, &rx, &ry);
        draw_needle(frame, w, h, bx, by, rx, ry, (int32_t)c->field.bar_width_q16,
                    c->field.bar_shape, &a->release_col, a->release_op);
    }
    if (final_len > release && a->tip_op != 0u) {
        field_point(g, final_len, &tx, &ty);
        draw_needle(frame, w, h, rx, ry, tx, ty, (int32_t)c->field.bar_width_q16,
                    c->field.bar_shape, &a->tip_col, a->tip_op);
    }
}

static void draw_orbit_ring(const odm_layered_config *c,const odm_layered_frame_plan*p,
                            odm_layered_pixel16 *frame){
    int64_t half;
    if (c->core.shape == ODM_CORE_SHAPE_CIRCLE) {
        draw_orbit_ring_circle(c,p,frame);
        return;
    }
    half=(int64_t)c->field.bar_width_q16/2;
    int64_t extent=(p->core_half_w_q16>p->core_half_h_q16?p->core_half_w_q16:p->core_half_h_q16)+
                   (int64_t)c->field.ring_gap_q16+half+65536;
    int64_t minx=((int64_t)p->center_x_q16-extent)>>16,maxx=((int64_t)p->center_x_q16+extent)>>16;
    int64_t miny=((int64_t)p->center_y_q16-extent)>>16,maxy=((int64_t)p->center_y_q16+extent)>>16,x,y;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= (int64_t)p->width) maxx = (int64_t)p->width - 1;
    if (maxy >= (int64_t)p->height) maxy = (int64_t)p->height - 1;
    for(y=miny;y<=maxy;++y)for(x=minx;x<=maxx;++x){
        int64_t dx=((x<<16)+32768)-p->center_x_q16,dy=((y<<16)+32768)-p->center_y_q16;
        int64_t sdf=odm_layered_core_sdf_q16(c,p,dx,dy);
        int64_t target=(int64_t)c->field.ring_gap_q16;
        uint32_t cov=odm_layered_coverage_q31(abs_i64(sdf-target),half,65536);
        if(cov)odm_layered_blend16(&frame[(uint64_t)y*p->width+(uint64_t)x],&c->field.secondary_color,cov,p->field_opacity_q31);
    }
}

void odm_layered_field_render(const odm_layered_config *c,
                              const odm_layered_frame_plan *p,
                              odm_layered_pixel16 *frame) {
    uint32_t i;
    if(!c||!p||!frame||p->field_opacity_q31==0u)return;
    if((c->field.flags&ODM_FIELD_ORBIT_RING)!=0u)draw_orbit_ring(c,p,frame);
    if((c->field.flags&ODM_FIELD_RADIAL_BARS)!=0u){
        /* COMPOSICIONES.
         *
         * Todas leen el MISMO `radial_q31` ya resuelto y la misma autoridad por
         * sector. Lo unico que cambia entre una y otra es de donde sale cada
         * sector y hacia donde crece. Por eso cambiar de composicion cambia el
         * cuadro y no cambia la cancion: la reaccion se decidio antes, arriba,
         * y aqui abajo ya no se puede reinterpretar. */
        const uint32_t segs = c->field.radial_segments;
        const int64_t safe_w = (int64_t)p->safe_right_q16 - (int64_t)p->safe_left_q16;
        /* BANDA LIBRE.
         *
         * Las composiciones rectas NUNCA cruzan el nucleo. Cruzarlo no es una
         * composicion: es un choque, y se lee como un descuido aunque la
         * reaccion sea exacta. El campo ocupa la franja libre mas alta -- la
         * que queda bajo el nucleo, ya descontada la banda del HUD, o la que
         * queda encima -- y solo si el nucleo no deja ninguna de las dos se
         * usa el area segura entera, porque entonces el diseno pidio un nucleo
         * que ocupa el cuadro y esa peticion manda. */
        const int64_t nucleo_top = (int64_t)p->center_y_q16 - (int64_t)p->core_half_h_q16;
        const int64_t nucleo_bot = (int64_t)p->center_y_q16 + (int64_t)p->core_half_h_q16;
        const int64_t hueco = (int64_t)c->field.ring_gap_q16;
        int64_t bajo_top = nucleo_bot + hueco;
        int64_t bajo_bot = (int64_t)p->safe_bottom_q16 - (int64_t)p->hud_reserved_q16;
        int64_t sobre_top = (int64_t)p->safe_top_q16;
        int64_t sobre_bot = nucleo_top - hueco;
        int64_t banda_top, banda_bot, banda_h;
        field_authority a;
        field_anchor g;
        if (bajo_top < (int64_t)p->safe_top_q16) bajo_top = (int64_t)p->safe_top_q16;
        if (sobre_bot > bajo_bot) sobre_bot = bajo_bot;
        /* La franja es SIEMPRE la de abajo. Es la lectura convencional -- la
         * imagen arriba, el espectro debajo -- y sobre todo es predecible: la
         * composicion no salta de sitio por un retoque del nucleo. Donde va el
         * nucleo lo decide el diseno con su posicion, no el rasterizador
         * eligiendo por su cuenta. Solo si el nucleo no deja franja alguna se
         * usa el area segura entera, porque entonces el diseno pidio un nucleo
         * que ocupa el cuadro y esa peticion manda. */
        banda_top = bajo_top; banda_bot = bajo_bot;
        banda_h = banda_bot - banda_top;
        if (banda_h <= 0) {
            banda_top = sobre_top; banda_bot = sobre_bot;
            banda_h = banda_bot - banda_top;
        }
        if (banda_h <= 0) {
            banda_top = (int64_t)p->safe_top_q16;
            banda_bot = (int64_t)p->safe_bottom_q16 - (int64_t)p->hud_reserved_q16;
            banda_h = banda_bot - banda_top;
        }
        switch (c->field.layout) {
        case ODM_FIELD_LAYOUT_LINEAR:
        case ODM_FIELD_LAYOUT_MIRROR: {
            /* Linea de barras. En espejo, cada sector crece hacia arriba y
             * hacia abajo desde el eje: la misma autoridad, dos direcciones. */
            const int espejo = c->field.layout == ODM_FIELD_LAYOUT_MIRROR;
            int64_t alcance = espejo ? (banda_h * 9) / 20 : (banda_h * 9) / 10;
            int64_t base_y = espejo ? (banda_top + banda_h / 2) : banda_bot;
            int64_t paso = segs > 0u ? safe_w / (int64_t)segs : 0;
            for (i = 0u; i < segs; ++i) {
                int64_t x = (int64_t)p->safe_left_q16 + paso / 2 + paso * (int64_t)i;
                field_segment_authority(c, p, i, &a);
                memset(&g, 0, sizeof(g));
                g.ax = (int32_t)x; g.ay = (int32_t)base_y;
                g.ux = 0; g.uy = -INT32_MAX;
                field_draw_segment(frame, p->width, p->height, c, &a, &g, alcance, 0);
                if (espejo) {
                    g.uy = INT32_MAX;
                    field_draw_segment(frame, p->width, p->height, c, &a, &g, alcance, 0);
                }
            }
            break;
        }
        case ODM_FIELD_LAYOUT_WAVE: {
            /* Onda continua: el sector no es una aguja sino el tramo que une su
             * altura con la del vecino. La lectura deja de ser "cuanto vale
             * cada banda" y pasa a ser "que forma tiene el espectro". */
            int64_t alcance = (banda_h * 9) / 20;
            int64_t eje = banda_top + banda_h / 2;
            int64_t paso = segs > 1u ? safe_w / (int64_t)(segs - 1u) : safe_w;
            int32_t px = 0, py = 0;
            for (i = 0u; i < segs; ++i) {
                int64_t x = (int64_t)p->safe_left_q16 + paso * (int64_t)i;
                int64_t alt;
                int32_t qx, qy;
                uint32_t op;
                odm_rgba16 col;
                field_segment_authority(c, p, i, &a);
                alt = field_len_q16(alcance, a.final_q31);
                qx = (int32_t)x; qy = (int32_t)(eje - alt);
                op = a.provenance ? a.body_op : a.simple_op;
                col = a.provenance ? a.body_col : a.simple_col;
                if (i > 0u && op != 0u)
                    draw_needle(frame, p->width, p->height, px, py, qx, qy,
                                (int32_t)c->field.bar_width_q16, c->field.bar_shape,
                                &col, op);
                px = qx; py = qy;
            }
            break;
        }
        case ODM_FIELD_LAYOUT_GRID: {
            /* Ecualizador de celdas: cada columna enciende tantas celdas como
             * dice su medida. Es la misma medida cuantizada para leerse, no una
             * medida distinta -- la celda superior encendida marca el valor. */
            const uint32_t filas = 12u;
            int64_t alto = (banda_h * 9) / 10;
            int64_t celda_h = alto / (int64_t)filas;
            int64_t paso = segs > 0u ? safe_w / (int64_t)segs : 0;
            uint32_t k;
            for (i = 0u; i < segs; ++i) {
                int64_t x = (int64_t)p->safe_left_q16 + paso / 2 + paso * (int64_t)i;
                uint32_t encendidas;
                uint32_t op;
                odm_rgba16 col;
                field_segment_authority(c, p, i, &a);
                encendidas = (uint32_t)(((uint64_t)a.final_q31 * filas) / (uint64_t)INT32_MAX);
                op = a.provenance ? a.body_op : a.simple_op;
                col = a.provenance ? a.body_col : a.simple_col;
                if (op == 0u) continue;
                for (k = 0u; k < encendidas && k < filas; ++k) {
                    int64_t y0 = banda_bot - celda_h * (int64_t)k;
                    int64_t y1 = y0 - celda_h / 2;
                    draw_needle(frame, p->width, p->height, (int32_t)x, (int32_t)y0,
                                (int32_t)x, (int32_t)y1,
                                (int32_t)c->field.bar_width_q16, c->field.bar_shape,
                                &col, op);
                }
                /* La punta del ataque se marca sobre la celda siguiente. */
                if (a.provenance && a.tip_op != 0u && encendidas < filas) {
                    int64_t y0 = banda_bot - celda_h * (int64_t)encendidas;
                    int64_t y1 = y0 - celda_h / 2;
                    draw_needle(frame, p->width, p->height, (int32_t)x, (int32_t)y0,
                                (int32_t)x, (int32_t)y1,
                                (int32_t)c->field.bar_width_q16, c->field.bar_shape,
                                &a.tip_col, a.tip_op);
                }
            }
            break;
        }
        default:
            for (i = 0u; i < segs; ++i) {
                uint32_t phase=p->ring_phase+(uint32_t)(((uint64_t)i<<32)/segs);
                int32_t cs=cos_q31(phase),sn=sin_q31(phase);
                field_segment_authority(c, p, i, &a);
                memset(&g, 0, sizeof(g));
                g.radial = 1;
                g.cx = p->center_x_q16; g.cy = p->center_y_q16;
                g.r0 = (int64_t)core_boundary_radius_q16(c,p,cs,sn)+(int64_t)c->field.ring_gap_q16;
                g.ux = cs; g.uy = sn;
                field_draw_segment(frame, p->width, p->height, c, &a, &g,
                                   (int64_t)c->field.bar_max_q16,
                                   a.provenance ? 0 : (int64_t)c->field.bar_min_q16);
            }
            break;
        }
    }
    if((c->field.flags&ODM_FIELD_PARTICLES)!=0u&&p->particle_count>0u&&p->particle_sim!=0u){
        /* CAMPO SIMULADO.
         *
         * Aqui no se calcula NINGUNA posicion: ya vienen decididas en el plan
         * por la integracion de 100 Hz. El rasterizador solo elige silueta,
         * tamano, brillo y si algo la tapa. Esa separacion es la que hace que
         * el mismo proyecto se vea igual a 24, 30 o 60 fps -- lo que cambia con
         * los FPS es cuando se mira, no donde esta el aire. */
        uint32_t dep = c->field.particle_depth_q31;
        uint32_t segments = c->field.radial_segments;
        uint32_t golpe;
        {   uint64_t acc = 0u; uint32_t k;
            for (k = 0u; k < segments; ++k) acc += p->radial_q31[k];
            golpe = (uint32_t)((acc + segments / 2u) / segments);
        }
        for (i = 0u; i < p->particle_count && i < ODM_PARTICLES_MAX; ++i) {
            uint64_t h2 = mix64(mix64(UINT64_C(0x51a7c0de9e3779b9) ^
                                      ((uint64_t)i * UINT64_C(0x9e3779b97f4a7c15))) ^
                                UINT64_C(0x5bf03635));
            uint32_t z = (uint32_t)((h2 >> 16) & UINT64_C(0xffff));
            int32_t x = p->particle_x_q16[i], y = p->particle_y_q16[i];
            int32_t radio_px = (int32_t)c->field.particle_radius_q16;
            uint32_t op;

            /* Oclusion: una particula lejana que cae dentro de la silueta del
             * nucleo esta DETRAS de el. Sin esto el campo seria una textura
             * plana con tamanos distintos, no un espacio. */
            if (dep != 0u && z < UINT32_C(32768)) {
                int64_t ndx=(int64_t)x-(int64_t)p->center_x_q16;
                int64_t ndy=(int64_t)y-(int64_t)p->center_y_q16;
                int64_t nd=(int64_t)odm_layered_isqrt_u64(
                    (uint64_t)((ndx>>8)*(ndx>>8)+(ndy>>8)*(ndy>>8)))<<8;
                int32_t pcs2 = nd > 0 ? (int32_t)((ndx*INT32_MAX)/nd) : INT32_MAX;
                int32_t psn2 = nd > 0 ? (int32_t)((ndy*INT32_MAX)/nd) : 0;
                if (nd < (int64_t)core_boundary_radius_q16(c,p,pcs2,psn2)) continue;
            }

            op=(uint32_t)(((uint64_t)p->field_opacity_q31 *
                           (UINT64_C(429496729) + (uint64_t)golpe / 2u)) / INT32_MAX);
            if (dep != 0u) {
                int64_t t = INT64_C(36044) + ((int64_t)z * INT64_C(58982)) / INT64_C(65536);
                int64_t k = 65536 + (((t - 65536) * (int64_t)dep) / INT32_MAX);
                radio_px = (int32_t)(((int64_t)radio_px * k) / INT64_C(65536));
                if (radio_px < (1 << 12)) radio_px = 1 << 12;
                op = (uint32_t)(((uint64_t)op * (uint64_t)k) / UINT64_C(65536));
            }
            if (op > (uint32_t)INT32_MAX) op = (uint32_t)INT32_MAX;
            if (op != 0u)
                draw_particle(frame, p->width, p->height, x, y, radio_px,
                              c->field.particle_shape,
                              p->particle_vx_q16[i], p->particle_vy_q16[i],
                              &c->field.particle_color, op);
        }
    } else if((c->field.flags&ODM_FIELD_PARTICLES)!=0u&&p->particle_count>0u){
        int32_t maxr=(int32_t)(((uint64_t)(p->width<p->height?p->width:p->height)*UINT64_C(65536)*48u)/100u/2u);
        int32_t range=maxr-p->ring_inner_radius_q16;
        if(range>0){
            if ((p->flags & ODM_COMPOSITION_FLAG_STRICT_CAUSAL) != 0u) {
                /* POLVO EN EL AIRE, NO UN ANILLO.
                 *
                 * Las particulas ocupan TODO el cuadro, como polvo suspendido,
                 * no una orbita alrededor del nucleo. Su sitio base sale de la
                 * semilla y es estable: la misma muestra da la misma imagen, y
                 * no hay reloj que las mueva -- lo que las mueve es la musica.
                 *
                 * La reaccion es LOCAL: el golpe empuja hacia fuera, y empuja
                 * mucho mas a las que estan cerca del nucleo que a las del
                 * borde. Eso es lo que hace que el aire parezca desplazado por
                 * algo que ocurre en el centro, en vez de una capa de puntos
                 * que parpadea entera a la vez.
                 *
                 * La profundidad decide tamano, brillo y cuanto la empuja el
                 * golpe; y las lejanas desaparecen tras la silueta del nucleo.
                 */
                uint32_t segments = c->field.radial_segments;
                uint32_t dep = c->field.particle_depth_q31;
                int64_t ref = (int64_t)(p->width < p->height ? p->width : p->height) << 15;
                uint32_t golpe;
                {   /* Un solo golpe global: la media exacta del campo radial. */
                    uint64_t acc = 0u;
                    uint32_t k;
                    for (k = 0u; k < segments; ++k) acc += p->radial_q31[k];
                    golpe = (uint32_t)((acc + segments / 2u) / segments);
                }
                for(i=0u;i<p->particle_count;++i){
                    uint64_t h=mix64(UINT64_C(0x51a7c0de9e3779b9) ^
                                     ((uint64_t)i * UINT64_C(0x9e3779b97f4a7c15)));
                    uint64_t h2=mix64(h ^ UINT64_C(0x5bf03635));
                    uint32_t z=(uint32_t)((h2>>16)&UINT64_C(0xffff));
                    /* Sitio base en todo el lienzo. */
                    int64_t bx=(int64_t)(((h & UINT64_C(0xffffffff)) * (uint64_t)p->width) >> 32) << 16;
                    int64_t by=(int64_t)(((h2 & UINT64_C(0xffffffff)) * (uint64_t)p->height) >> 32) << 16;
                    int64_t dx=bx-(int64_t)p->center_x_q16;
                    int64_t dy=by-(int64_t)p->center_y_q16;
                    int64_t dist=(int64_t)odm_layered_isqrt_u64(
                        (uint64_t)((dx>>8)*(dx>>8)+(dy>>8)*(dy>>8)))<<8;
                    int64_t cercania, empuje;
                    int32_t x,y,radio_px;
                    uint32_t op;

                    /* Cercania al nucleo: 1 en el centro, 0 en la referencia. */
                    cercania = dist >= ref ? 0 : (((ref - dist) * INT32_MAX) / ref);

                    /* Empuje radial del golpe, mas fuerte cuanto mas cerca.
                     * Las cercanas al nucleo tambien pesan mas por profundidad. */
                    empuje = (((int64_t)golpe * cercania) / INT32_MAX);
                    if (dep != 0u)
                        empuje = (empuje * (INT64_C(32768) + (int64_t)z)) / INT64_C(65536);
                    /* Hasta un octavo del lado menor de desplazamiento. */
                    empuje = (empuje * (ref / 4)) / INT32_MAX;

                    if (dist > 0) {
                        x=(int32_t)(bx + (dx * empuje) / dist);
                        y=(int32_t)(by + (dy * empuje) / dist);
                    } else { x=(int32_t)bx; y=(int32_t)by; }

                    /* Oclusion: una lejana dentro de la silueta esta detras. */
                    if (dep != 0u && z < UINT32_C(32768)) {
                        int64_t ndx=(int64_t)x-(int64_t)p->center_x_q16;
                        int64_t ndy=(int64_t)y-(int64_t)p->center_y_q16;
                        int64_t nd=(int64_t)odm_layered_isqrt_u64(
                            (uint64_t)((ndx>>8)*(ndx>>8)+(ndy>>8)*(ndy>>8)))<<8;
                        int32_t pcs2 = nd > 0 ? (int32_t)((ndx*INT32_MAX)/nd) : INT32_MAX;
                        int32_t psn2 = nd > 0 ? (int32_t)((ndy*INT32_MAX)/nd) : 0;
                        if (nd < (int64_t)core_boundary_radius_q16(c,p,pcs2,psn2)) continue;
                    }

                    /* El polvo respira con el golpe, pero nunca desaparece:
                     * un suelo de presencia mantiene el aire habitado. */
                    op=(uint32_t)(((uint64_t)p->field_opacity_q31 *
                                   (UINT64_C(429496729) + (uint64_t)golpe / 2u)) / INT32_MAX);
                    radio_px=(int32_t)c->field.particle_radius_q16;
                    if (dep != 0u) {
                        int64_t t = INT64_C(36044) + ((int64_t)z * INT64_C(58982)) / INT64_C(65536);
                        int64_t k = 65536 + (((t - 65536) * (int64_t)dep) / INT32_MAX);
                        radio_px = (int32_t)(((int64_t)radio_px * k) / INT64_C(65536));
                        if (radio_px < (1 << 12)) radio_px = 1 << 12;
                        op = (uint32_t)(((uint64_t)op * (uint64_t)k) / UINT64_C(65536));
                    }
                    if (op > (uint32_t)INT32_MAX) op = (uint32_t)INT32_MAX;
                    if (op != 0u)
                        draw_disk(frame,p->width,p->height,x,y,radio_px,&c->field.particle_color,op);
                }
            } else {
                for(i=0u;i<p->particle_count;++i){
                    uint64_t h=mix64(c->field.seed^((uint64_t)i*UINT64_C(0x9e3779b97f4a7c15)));
                    uint32_t base=(uint32_t)h;
                    uint32_t speed=UINT32_C(20000)+(uint32_t)((h>>32)&UINT32_C(0xffff));
                    uint32_t phase=base+particle_phase_advance(p->sample,speed)+p->ring_phase;
                    uint32_t rq=(uint32_t)(mix64(h^UINT64_C(0x5bf03635))>>33);
                    int32_t pcs=cos_q31(phase),psn=sin_q31(phase);
                    int64_t boundary=(int64_t)core_boundary_radius_q16(c,p,pcs,psn)+(int64_t)c->field.ring_gap_q16;
                    int64_t r=boundary+((int64_t)range*rq)/INT32_MAX;
                    int32_t x=(int32_t)((int64_t)p->center_x_q16+(r*pcs)/INT32_MAX);
                    int32_t y=(int32_t)((int64_t)p->center_y_q16+(r*psn)/INT32_MAX);
                    uint32_t flicker=(uint32_t)(mix64(h^p->tick_index)>>33);
                    uint32_t op=(uint32_t)(((uint64_t)p->field_opacity_q31*(INT32_MAX/3u+(uint64_t)flicker*2u/3u)+INT32_MAX/2u)/INT32_MAX);
                    draw_disk(frame,p->width,p->height,x,y,(int32_t)c->field.particle_radius_q16,&c->field.particle_color,op);
                }
            }
        }
    }
}
