#include "compositor_internal.h"

#include <limits.h>
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

/* Segment rasterization is canonically Q24.8.  Projection uses Q16 t so all
 * products fit signed 64-bit for the admitted 16k canvas / 2k bar limits. */
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
        for(i=0u;i<c->field.radial_segments;++i){
            uint32_t phase=p->ring_phase+(uint32_t)(((uint64_t)i<<32)/c->field.radial_segments);
            int32_t cs=cos_q31(phase),sn=sin_q31(phase);
            int64_t r0=(int64_t)core_boundary_radius_q16(c,p,cs,sn)+(int64_t)c->field.ring_gap_q16;
            int32_t ax=(int32_t)((int64_t)p->center_x_q16+(r0*cs)/INT32_MAX);
            int32_t ay=(int32_t)((int64_t)p->center_y_q16+(r0*sn)/INT32_MAX);
            if ((p->flags & ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE) != 0u) {
                /* Provenance raster: the orbit/core contour is already the
                 * stable base. Reactive bars therefore have no autonomous
                 * minimum length. Held spectral body is a subdued segment;
                 * same-lane attack is a separate tip whose length and opacity
                 * are both music-authoritative. */
                int64_t body_len=((int64_t)c->field.bar_max_q16*
                                  (int64_t)p->radial_body_q31[i]+INT32_MAX/2)/INT32_MAX;
                uint32_t release_base_q31=p->radial_body_q31[i];
                int64_t release_len;
                int64_t final_len=((int64_t)c->field.bar_max_q16*
                                   (int64_t)p->radial_q31[i]+INT32_MAX/2)/INT32_MAX;
                int64_t rb=r0+body_len;
                int32_t bdx=(int32_t)((int64_t)p->center_x_q16+(rb*cs)/INT32_MAX);
                int32_t bdy=(int32_t)((int64_t)p->center_y_q16+(rb*sn)/INT32_MAX);
                int32_t rdx=bdx,rdy=bdy;
                if ((p->flags & ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE) != 0u) {
                    const uint32_t release_weight_q31=UINT32_C(751619277); /* 0.35 */
                    uint32_t release_mix=field_mul_q31(p->radial_release_q31[i],release_weight_q31);
                    release_base_q31 += field_mul_q31((uint32_t)INT32_MAX-release_base_q31,release_mix);
                }
                release_len=((int64_t)c->field.bar_max_q16*(int64_t)release_base_q31+INT32_MAX/2)/INT32_MAX;
                if (body_len > 0) {
                    /* Dual-domain optical authority. Sustained spectrum keeps
                     * a faint body, but a bright protrusion requires same-lane
                     * transient/release evidence. This prevents dense songs
                     * from leaving a static white crown around the Core. */
                    uint32_t local_activity = p->radial_attack_q31[i];
                    uint32_t tail_activity = field_mul_q31(p->radial_release_q31[i],
                                                          UINT32_C(1395864371)); /* 0.65 */
                    uint32_t activity = local_activity > tail_activity ? local_activity : tail_activity;
                    uint32_t authority = UINT32_C(322122547); /* 0.15 floor */
                    authority += field_mul_q31((uint32_t)INT32_MAX - authority, activity);
                    draw_segment(frame,p->width,p->height,ax,ay,bdx,bdy,
                                 (int32_t)c->field.bar_width_q16,
                                 &c->field.secondary_color,
                                 field_mul_q31(p->field_opacity_q31, authority));
                }
                if (release_len > body_len && p->radial_release_q31[i] != 0u) {
                    int64_t rr=r0+release_len;
                    uint32_t release_opacity=field_mul_q31(p->field_opacity_q31,
                                                          p->radial_release_q31[i]);
                    rdx=(int32_t)((int64_t)p->center_x_q16+(rr*cs)/INT32_MAX);
                    rdy=(int32_t)((int64_t)p->center_y_q16+(rr*sn)/INT32_MAX);
                    if (release_opacity != 0u)
                        draw_segment(frame,p->width,p->height,bdx,bdy,rdx,rdy,
                                     (int32_t)c->field.bar_width_q16,
                                     &c->field.secondary_color,release_opacity);
                }
                if (final_len > release_len && p->radial_attack_q31[i] != 0u) {
                    int64_t rf=r0+final_len;
                    int32_t tx=(int32_t)((int64_t)p->center_x_q16+(rf*cs)/INT32_MAX);
                    int32_t ty=(int32_t)((int64_t)p->center_y_q16+(rf*sn)/INT32_MAX);
                    uint32_t tip_authority=field_mul_q31(p->radial_attack_q31[i],
                                                         p->radial_attack_q31[i]);
                    uint32_t tip_opacity=field_mul_q31(p->field_opacity_q31,
                                                      tip_authority);
                    if (tip_opacity != 0u)
                        draw_segment(frame,p->width,p->height,rdx,rdy,tx,ty,
                                     (int32_t)c->field.bar_width_q16,
                                     &c->field.primary_color,tip_opacity);
                }
            } else {
                int64_t span=(int64_t)c->field.bar_max_q16-(int64_t)c->field.bar_min_q16;
                int64_t len=(int64_t)c->field.bar_min_q16+
                    (span*(int64_t)p->radial_q31[i]+INT32_MAX/2)/INT32_MAX;
                int64_t r1=r0+len;
                int32_t bx=(int32_t)((int64_t)p->center_x_q16+(r1*cs)/INT32_MAX);
                int32_t by=(int32_t)((int64_t)p->center_y_q16+(r1*sn)/INT32_MAX);
                const odm_rgba16 *col=(i&1u)?&c->field.secondary_color:&c->field.primary_color;
                draw_segment(frame,p->width,p->height,ax,ay,bx,by,
                             (int32_t)c->field.bar_width_q16,col,p->field_opacity_q31);
            }
        }
    }
    if((c->field.flags&ODM_FIELD_PARTICLES)!=0u&&p->particle_count>0u){
        int32_t maxr=(int32_t)(((uint64_t)(p->width<p->height?p->width:p->height)*UINT64_C(65536)*48u)/100u/2u);
        int32_t range=maxr-p->ring_inner_radius_q16;
        if(range>0){
            if ((p->flags & ODM_COMPOSITION_FLAG_STRICT_CAUSAL) != 0u) {
                /* Strict Music-Reaction particles are spatially indexed by the
                 * causal radial field. Their placement is seed-stable, while
                 * visibility comes only from the corresponding music-derived
                 * radial amplitude. No sample/tick clock advances position or
                 * opacity in this path. */
                uint32_t segments = c->field.radial_segments;
                for(i=0u;i<p->particle_count;++i){
                    uint64_t h=mix64(UINT64_C(0x51a7c0de9e3779b9) ^
                                     ((uint64_t)i * UINT64_C(0x9e3779b97f4a7c15)));
                    uint32_t sector=(uint32_t)(((uint64_t)i * UINT64_C(73)) % (uint64_t)segments);
                    uint64_t cell=UINT64_C(0x100000000)/(uint64_t)segments;
                    uint64_t base=((uint64_t)sector*UINT64_C(0x100000000))/(uint64_t)segments;
                    uint64_t jitter=((h>>48u)&UINT64_C(0xffff))*cell/UINT64_C(65536);
                    uint32_t phase=(uint32_t)(base+jitter);
                    uint32_t amp=p->radial_q31[sector];
                    uint32_t rq=(uint32_t)(mix64(h^UINT64_C(0x5bf03635))>>33);
                    int32_t pcs,psn;
                    int64_t boundary,r;
                    int32_t x,y;
                    uint32_t op;
                    if(amp==0u)continue;
                    pcs=cos_q31(phase);psn=sin_q31(phase);
                    boundary=(int64_t)core_boundary_radius_q16(c,p,pcs,psn)+(int64_t)c->field.ring_gap_q16;
                    /* The base scatter is fixed, but radial distance is
                     * music-authoritative. 25..50% of the available range is
                     * static topology; the remaining 0..50% is driven by this
                     * sector's exact radial amplitude. No wall-clock term is
                     * present, so a held spectrum produces a held position. */
                    {
                        int64_t base_q31 = INT64_C(536870912) + (int64_t)rq / 4;
                        int64_t music_q31 = (int64_t)amp / 2;
                        int64_t radius_q31 = base_q31 + music_q31;
                        if (radius_q31 > INT32_MAX) radius_q31 = INT32_MAX;
                        r=boundary+((int64_t)range*radius_q31+INT32_MAX/2)/INT32_MAX;
                    }
                    x=(int32_t)((int64_t)p->center_x_q16+(r*pcs)/INT32_MAX);
                    y=(int32_t)((int64_t)p->center_y_q16+(r*psn)/INT32_MAX);
                    op=(uint32_t)(((uint64_t)p->field_opacity_q31*(uint64_t)amp+INT32_MAX/2u)/INT32_MAX);
                    draw_disk(frame,p->width,p->height,x,y,(int32_t)c->field.particle_radius_q16,&c->field.primary_color,op);
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
                    draw_disk(frame,p->width,p->height,x,y,(int32_t)c->field.particle_radius_q16,&c->field.primary_color,op);
                }
            }
        }
    }
}
