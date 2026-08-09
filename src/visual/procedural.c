#include "visual_internal.h"
#include "odm_wire.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define Q31_010 INT32_C(214748365)
#define Q31_018 INT32_C(386547057)
#define Q31_025 INT32_C(536870912)
#define Q31_035 INT32_C(751619277)
#define Q31_055 INT32_C(1181116006)
#define Q31_070 INT32_C(1503238553)
#define Q31_090 INT32_C(1932735282)

static odm_status qmul(odm_q1_31 a, odm_q1_31 b, odm_q1_31 *out) {
    if (!out) return ODM_STATUS_INVALID_ARGUMENT;
    if (a == 0 || b == 0) { *out = 0; return ODM_STATUS_OK; }
    if (a == INT32_MAX) { *out = b; return ODM_STATUS_OK; }
    if (b == INT32_MAX) { *out = a; return ODM_STATUS_OK; }
    return odm_q1_31_mul(a, b, out);
}

static odm_q1_31 sat_add_q31(odm_q1_31 a, odm_q1_31 b) {
    int64_t v = (int64_t)a + (int64_t)b;
    if (v > INT32_MAX) return INT32_MAX;
    if (v < 0) return 0;
    return (odm_q1_31)v;
}

static int64_t q32_to_pixel(odm_q32_32 q, uint32_t span) {
    /* q is constitutionally clamped to +/-64.0 and span <= 16384, so the
     * product is bounded by 2^52 and is safely representable in int64_t. */
    int64_t v = q * (int64_t)span;
    return v / INT64_C(8589934592); /* 2 * 2^32: one unit == half span */
}

static odm_status premul(odm_q1_31 r, odm_q1_31 g, odm_q1_31 b,
                         odm_q1_31 a, odm_visual_pixel_q31 *out) {
    odm_status st;
    st=qmul(r,a,&out->r);if(st!=ODM_STATUS_OK)return st;
    st=qmul(g,a,&out->g);if(st!=ODM_STATUS_OK)return st;
    st=qmul(b,a,&out->b);if(st!=ODM_STATUS_OK)return st;
    out->a=a;return ODM_STATUS_OK;
}

static odm_status grid_pixel(const odm_frame_node_state*n,uint32_t x,uint32_t y,
                             odm_visual_pixel_q31*out){
    uint32_t spacing=24u+((n->phase>>28u)&15u),off=(n->phase>>20u)%spacing;
    int line=(((x+off)%spacing)<2u)||(((y+off)%spacing)<2u);odm_q1_31 a=0;odm_status st;
    if(line){st=qmul(n->opacity_q31,Q31_025,&a);if(st!=ODM_STATUS_OK)return st;}
    return premul(Q31_010,Q31_035,Q31_055,a,out);
}

static odm_status halo_pixel(const odm_frame_node_state*n,uint32_t x,uint32_t y,
                             uint32_t w,uint32_t h,odm_visual_pixel_q31*out){
    int64_t cx=(int64_t)(w/2u)+q32_to_pixel(n->x_q32_32,w),cy=(int64_t)(h/2u)+q32_to_pixel(n->y_q32_32,h);
    uint32_t base=(w<h?w:h)/5u,span=(w<h?w:h)/10u;uint32_t radius=base+(uint32_t)(((uint64_t)(n->phase>>16u)*span)>>16u);uint32_t thick=(w<h?w:h)/64u+1u;
    int64_t dx=(int64_t)x-cx,dy=(int64_t)y-cy;uint64_t d2=(uint64_t)(dx*dx)+(uint64_t)(dy*dy);uint64_t lo=(uint64_t)(radius>thick?radius-thick:0u);uint64_t hi=(uint64_t)radius+thick;odm_q1_31 a=0;odm_status st;
    lo*=lo;hi*=hi;if(d2>=lo&&d2<=hi){st=qmul(n->opacity_q31,Q31_055,&a);if(st!=ODM_STATUS_OK)return st;}
    return premul(Q31_018,Q31_055,Q31_090,a,out);
}

static odm_status residual_pixel(const odm_frame_node_state*n,const odm_visual_history*h,
                                 uint32_t x,uint32_t y,uint32_t w,uint32_t ht,
                                 odm_visual_pixel_q31*out){
    uint32_t i;odm_q1_31 alpha=0;
    for(i=0u;i<h->event_count;++i){const odm_visual_event*e=&h->events[i];uint64_t age=(uint64_t)e->age_samples/480u;uint32_t life=240u;if(age>=life)continue;uint32_t cx=(uint32_t)(e->seed%(uint64_t)w),cy=(uint32_t)((e->seed>>32u)%(uint64_t)ht),radius=3u+(uint32_t)(age/12u);int64_t dx=(int64_t)x-(int64_t)cx,dy=(int64_t)y-(int64_t)cy;uint64_t d2=(uint64_t)(dx*dx)+(uint64_t)(dy*dy);if(d2<=(uint64_t)radius*radius){odm_q1_31 decay=(odm_q1_31)(((uint64_t)(life-(uint32_t)age)*(uint64_t)INT32_MAX)/life),a;odm_status st=qmul(n->opacity_q31,decay,&a);if(st!=ODM_STATUS_OK)return st;st=qmul(a,Q31_018,&a);if(st!=ODM_STATUS_OK)return st;alpha=sat_add_q31(alpha,a);}}
    return premul(Q31_010,Q31_035,Q31_070,alpha,out);
}

static int64_t wrap_coord(int64_t value,uint32_t span){int64_t s=(int64_t)span;value%=s;if(value<0)value+=s;return value;}

static odm_status particles_pixel(const odm_frame_node_state*n,const odm_visual_history*h,
                                  uint32_t x,uint32_t y,uint32_t w,uint32_t ht,
                                  odm_visual_pixel_q31*out){
    uint32_t i,j;odm_q1_31 alpha=0;
    for(i=0u;i<h->event_count;++i){const odm_visual_event*e=&h->events[i];uint64_t age=(uint64_t)e->age_samples/480u;uint32_t life=180u;if(age>=life)continue;for(j=0u;j<4u;++j){uint64_t s=e->seed^(UINT64_C(0x9e3779b97f4a7c15)*(uint64_t)(j+1u));int64_t px=(int64_t)(s%(uint64_t)w),py=(int64_t)((s>>32u)%(uint64_t)ht);int32_t vx=(int32_t)((s>>16u)&7u)-3,vy=(int32_t)((s>>24u)&7u)-4;px=wrap_coord(px+(int64_t)vx*(int64_t)age/4,w);py=wrap_coord(py+(int64_t)vy*(int64_t)age/4,ht);if((px-(int64_t)x<=1&&px-(int64_t)x>=-1)&&(py-(int64_t)y<=1&&py-(int64_t)y>=-1)){odm_q1_31 decay=(odm_q1_31)(((uint64_t)(life-(uint32_t)age)*(uint64_t)INT32_MAX)/life),a;odm_status st=qmul(n->opacity_q31,decay,&a);if(st!=ODM_STATUS_OK)return st;st=qmul(a,Q31_035,&a);if(st!=ODM_STATUS_OK)return st;alpha=sat_add_q31(alpha,a);}}}
    return premul(Q31_055,Q31_070,Q31_090,alpha,out);
}

odm_status odm_visual_procedural_pixel(const odm_frame_node_state *node,
                                       const odm_visual_history *history,
                                       int64_t sample,uint32_t x,uint32_t y,
                                       uint32_t width,uint32_t height,
                                       odm_visual_pixel_q31 *out_pixel){
    (void)sample;if(!node||!out_pixel||width==0u||height==0u||x>=width||y>=height)return ODM_STATUS_INVALID_ARGUMENT;memset(out_pixel,0,sizeof(*out_pixel));
    switch(node->capability_id){
      case ODM_CAP_GRID_PROOF:return grid_pixel(node,x,y,out_pixel);
      case ODM_CAP_HALO:return halo_pixel(node,x,y,width,height,out_pixel);
      case ODM_CAP_RESIDUAL_EVENT_FIELD:if(!history)return ODM_STATUS_INVALID_ARGUMENT;return residual_pixel(node,history,x,y,width,height,out_pixel);
      case ODM_CAP_DETERMINISTIC_PARTICLES:if(!history)return ODM_STATUS_INVALID_ARGUMENT;return particles_pixel(node,history,x,y,width,height,out_pixel);
      default:return ODM_STATUS_UNSUPPORTED;
    }
}

/* ================= Advanced Composition Layer v1 ================= */

#define COMP_Q31_005 UINT32_C(107374182)
#define COMP_Q31_008 UINT32_C(171798692)
#define COMP_Q31_010 UINT32_C(214748365)
#define COMP_Q31_012 UINT32_C(257698038)
#define COMP_Q31_015 UINT32_C(322122547)
#define COMP_Q31_018 UINT32_C(386547057)
#define COMP_Q31_020 UINT32_C(429496730)
#define COMP_Q31_025 UINT32_C(536870912)
#define COMP_Q31_030 UINT32_C(644245094)
#define COMP_Q31_035 UINT32_C(751619277)
#define COMP_Q31_040 UINT32_C(858993459)
#define COMP_Q31_045 UINT32_C(966367642)
#define COMP_Q31_048 UINT32_C(1030792151)
#define COMP_Q31_050 UINT32_C(1073741824)
#define COMP_Q31_055 UINT32_C(1181116006)
#define COMP_Q31_058 UINT32_C(1245540516)
#define COMP_Q31_060 UINT32_C(1288490189)
#define COMP_Q31_065 UINT32_C(1395864371)
#define COMP_Q31_070 UINT32_C(1503238553)
#define COMP_Q31_075 UINT32_C(1610612735)
#define COMP_Q31_080 UINT32_C(1717986918)
#define COMP_Q31_085 UINT32_C(1825361100)
#define COMP_Q31_090 UINT32_C(1932735282)
#define COMP_Q31_ONE UINT32_C(2147483647)

static uint32_t comp_sat_u31(uint64_t value) {
    return value > (uint64_t)COMP_Q31_ONE ? COMP_Q31_ONE : (uint32_t)value;
}

static uint32_t comp_max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }

static uint32_t comp_smooth_u31(uint32_t current, uint32_t target,
                                uint32_t shift) {
    int64_t delta = (int64_t)(uint64_t)target - (int64_t)(uint64_t)current;
    int64_t next = (int64_t)(uint64_t)current + delta / (INT64_C(1) << shift);
    if (next < 0) return 0u;
    if ((uint64_t)next > (uint64_t)COMP_Q31_ONE) return COMP_Q31_ONE;
    return (uint32_t)next;
}

static uint32_t comp_mul_u31(uint32_t a, uint32_t b) {
    uint64_t p = (uint64_t)a * (uint64_t)b;
    p += UINT64_C(1073741823);
    p /= UINT64_C(2147483647);
    return comp_sat_u31(p);
}

static uint32_t comp_add_u31(uint32_t a, uint32_t b) {
    return comp_sat_u31((uint64_t)a + (uint64_t)b);
}

static uint32_t comp_lerp_u31(uint32_t lo, uint32_t hi, uint32_t t) {
    uint32_t span;
    if (hi <= lo) return lo;
    span = hi - lo;
    return comp_add_u31(lo, comp_mul_u31(span, t));
}

/* Map [threshold,1] -> [0,1] exactly in Q1.31. This is used only to
 * partition contextual presentation authority; it never modifies raw music
 * evidence. Values below/equal threshold have no authority on that axis. */
static uint32_t comp_gate_u31(uint32_t value, uint32_t threshold) {
    uint64_t numerator;
    uint64_t denominator;
    uint64_t q;
    if (threshold >= COMP_Q31_ONE || value <= threshold) return 0u;
    if (value >= COMP_Q31_ONE) return COMP_Q31_ONE;
    numerator = (uint64_t)(value - threshold) * (uint64_t)COMP_Q31_ONE;
    denominator = (uint64_t)(COMP_Q31_ONE - threshold);
    q = (numerator + denominator / 2u) / denominator;
    return comp_sat_u31(q);
}

/* Perceptual soft knee: strict zero below lo, unity above hi and a cubic
 * smoothstep in between. This suppresses low-authority residual shafts without
 * introducing a hard frame-visible switch. */
static uint32_t comp_soft_knee_u31(uint32_t value, uint32_t lo, uint32_t hi) {
    uint32_t t, t2, t3;
    uint64_t a, b, r;
    if (value <= lo) return 0u;
    if (value >= hi || hi <= lo) return COMP_Q31_ONE;
    t = (uint32_t)((((uint64_t)(value - lo) * (uint64_t)COMP_Q31_ONE) +
                    (uint64_t)(hi - lo) / 2u) / (uint64_t)(hi - lo));
    t2 = comp_mul_u31(t, t);
    t3 = comp_mul_u31(t2, t);
    a = (uint64_t)t2 * UINT64_C(3);
    b = (uint64_t)t3 * UINT64_C(2);
    r = a > b ? a - b : 0u;
    return comp_sat_u31(r);
}

static int32_t comp_triangle_q31(uint32_t phase) {
    uint32_t p = phase >> 1u;
    int64_t v;
    if ((phase & UINT32_C(0x80000000)) != 0u) p = UINT32_MAX - p;
    v = (int64_t)(uint64_t)(p >> 1u) - INT64_C(536870912);
    v *= 2;
    if (v > INT32_MAX) v = INT32_MAX;
    if (v < INT32_MIN) v = INT32_MIN;
    return (int32_t)v;
}

static int32_t comp_scaled_signed(int32_t wave, uint32_t gain) {
    int64_t p = (int64_t)wave * (int64_t)(uint64_t)gain;
    p /= INT64_C(2147483647);
    if (p > INT32_MAX) p = INT32_MAX;
    if (p < INT32_MIN) p = INT32_MIN;
    return (int32_t)p;
}

static uint32_t comp_tick_u31(uint32_t v) {
    return v > COMP_Q31_ONE ? COMP_Q31_ONE : v;
}

static int comp_zero_words(const uint32_t *v, uint32_t n) {
    uint32_t i;
    if (!v) return 0;
    for (i = 0u; i < n; ++i) if (v[i] != 0u) return 0;
    return 1;
}

/* Fail-closed validation at the Visual boundary. Music-Reaction normally
 * produces these frames, but a public caller must not be able to bypass its
 * Q1.31, flag, timestamp or contextual-evidence invariants. */
static int comp_reaction_input_valid(const odm_music_analysis_tick *base,
                                     const odm_music_reaction_frame *r) {
    const uint32_t allowed_events = ODM_MUSIC_REACTION_EVENT_SUB |
                                    ODM_MUSIC_REACTION_EVENT_BASS |
                                    ODM_MUSIC_REACTION_EVENT_LOW_MID |
                                    ODM_MUSIC_REACTION_EVENT_BODY |
                                    ODM_MUSIC_REACTION_EVENT_PRESENCE |
                                    ODM_MUSIC_REACTION_EVENT_AIR |
                                    ODM_MUSIC_REACTION_EVENT_ANY |
                                    ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
    uint32_t i;
    if (!base || !r || r->schema_version != ODM_MUSIC_REACTION_SCHEMA_VERSION ||
        base->tick_index > UINT64_MAX / (uint64_t)ODM_MUSIC_TICK_SAMPLES ||
        base->center_sample != base->tick_index * (uint64_t)ODM_MUSIC_TICK_SAMPLES ||
        base->silence > 1u || base->stereo_width_q31 > COMP_Q31_ONE ||
        base->stereo_balance_q31 == INT32_MIN || !comp_zero_words(base->reserved, 7u) ||
        r->tick_index != base->tick_index || r->center_sample != base->center_sample ||
        (r->event_flags & ~allowed_events) != 0u ||
        r->broadband_q31 > COMP_Q31_ONE || r->broadband_attack_q31 > COMP_Q31_ONE ||
        r->event_strength_q31 > COMP_Q31_ONE || r->event_pulse_q31 > COMP_Q31_ONE ||
        r->event_threshold_q31 > COMP_Q31_ONE || r->macro_salience_q31 > COMP_Q31_ONE ||
        r->macro_pulse_q31 > COMP_Q31_ONE ||
        (r->frame_flags & ~ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE) != 0u ||
        !comp_zero_words(r->reserved, 8u)) return 0;
    if ((r->event_flags & ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK) != 0u &&
        (r->event_flags & ODM_MUSIC_REACTION_EVENT_ANY) == 0u) return 0;
    if ((r->frame_flags & ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE) == 0u &&
        (r->macro_salience_q31 != 0u || r->macro_pulse_q31 != 0u)) return 0;
    if (r->macro_salience_q31 != 0u &&
        (((r->event_flags & ODM_MUSIC_REACTION_EVENT_ANY) == 0u) ||
         r->event_strength_q31 == 0u || r->macro_pulse_q31 < r->macro_salience_q31)) return 0;
    for (i = 0u; i < ODM_MUSIC_REACTION_LANE_COUNT; ++i)
        if (r->lane_q31[i] > COMP_Q31_ONE || r->lane_fast_q31[i] > COMP_Q31_ONE ||
            r->lane_attack_q31[i] > COMP_Q31_ONE || r->lane_fast_q31[i] > r->lane_q31[i]) return 0;
    for (i = 0u; i < ODM_MUSIC_REACTION_FAMILY_COUNT; ++i)
        if (r->family_q31[i] > COMP_Q31_ONE || r->family_attack_q31[i] > COMP_Q31_ONE) return 0;
    return 1;
}

odm_status odm_composition_resolver_init(odm_composition_resolver_state *state,
                                         uint64_t seed) {
    if (!state) return ODM_STATUS_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    state->schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
    state->seed = seed;
    state->phase = (uint32_t)(seed ^ (seed >> 32u));
    return ODM_STATUS_OK;
}

odm_status odm_composition_resolve_tick(odm_composition_resolver_state *state,
                                        const odm_music_analysis_tick *tick,
                                        odm_composition_frame_state *out_frame) {
    odm_composition_resolver_state next;
    odm_composition_frame_state f;
    uint32_t energy, flux, positive_delta, impact, width, high, low, i;
    uint32_t decay, drift_gain, phase_step, glitch = 0u;
    int32_t wx, wy;

    if (!state || !tick || !out_frame) return ODM_STATUS_INVALID_ARGUMENT;
    if (state->schema_version != ODM_COMPOSITION_SCHEMA_VERSION)
        return ODM_STATUS_VERSION_MISMATCH;
    if (state->initialized != 0u && tick->tick_index != state->next_tick_index)
        return ODM_STATUS_INVALID_DATA;
    if (tick->tick_index == UINT64_MAX) return ODM_STATUS_OVERFLOW;
    if (tick->silence > 1u) return ODM_STATUS_INVALID_DATA;

    energy = comp_tick_u31(tick->envelope_medium_q31);
    flux = comp_tick_u31(tick->spectral_flux_q31);
    if (tick->energy_delta_q31 > 0) {
        positive_delta = comp_sat_u31((uint64_t)tick->energy_delta_q31);
    } else positive_delta = 0u;
    impact = comp_max_u32(flux, positive_delta);
    width = comp_tick_u31(tick->stereo_width_q31);
    low = comp_tick_u31(comp_max_u32(tick->band_energy_q31[0],
                                     tick->band_energy_q31[1]));
    high = comp_tick_u31(comp_max_u32(tick->band_energy_q31[4],
                                      tick->band_energy_q31[5]));

    next = *state;
    if (next.initialized == 0u) {
        next.energy_slow_q31 = energy;
        next.width_slow_q31 = width;
        next.high_slow_q31 = high;
        next.impact_hold_q31 = impact;
        next.initialized = 1u;
    } else {
        next.energy_slow_q31 = comp_smooth_u31(next.energy_slow_q31, energy, 3u);
        next.width_slow_q31 = comp_smooth_u31(next.width_slow_q31, width, 4u);
        next.high_slow_q31 = comp_smooth_u31(next.high_slow_q31, high, 3u);
        decay = next.impact_hold_q31 / 18u;
        if (decay == 0u && next.impact_hold_q31 != 0u) decay = 1u;
        next.impact_hold_q31 = next.impact_hold_q31 > decay ?
                               next.impact_hold_q31 - decay : 0u;
        next.impact_hold_q31 = comp_max_u32(next.impact_hold_q31, impact);
    }

    if (next.glitch_cooldown_ticks != 0u) --next.glitch_cooldown_ticks;
    if (impact >= COMP_Q31_018 && next.glitch_cooldown_ticks == 0u && tick->silence == 0u) {
        glitch = 1u;
        next.glitch_cooldown_ticks = 12u;
    }
    phase_step = UINT32_C(6291456) + (next.energy_slow_q31 >> 8u) +
                 (next.width_slow_q31 >> 10u);
    next.phase += phase_step;
    next.next_tick_index = tick->tick_index + 1u;

    memset(&f, 0, sizeof(f));
    f.schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
    f.tick_index = tick->tick_index;
    f.center_sample = tick->center_sample;
    f.phase = next.phase;
    f.ring_phase = next.phase + (uint32_t)(tick->tick_index * UINT64_C(2654435761));
    if (tick->silence != 0u) {
        f.mode = ODM_COMPOSITION_MODE_VOID;
        f.flags |= ODM_COMPOSITION_FLAG_SILENCE;
    } else if (next.impact_hold_q31 >= COMP_Q31_025) {
        f.mode = ODM_COMPOSITION_MODE_IMPACT;
    } else if (next.energy_slow_q31 >= COMP_Q31_010) {
        f.mode = ODM_COMPOSITION_MODE_FLOW;
    } else {
        f.mode = ODM_COMPOSITION_MODE_CALM;
    }
    if (glitch != 0u) f.flags |= ODM_COMPOSITION_FLAG_GLITCH_GATE;

    /* The core stays centered. Only its CONTENT drifts by <= 1.5% of span. */
    drift_gain = comp_mul_u31(COMP_Q31_015,
                              comp_add_u31(next.energy_slow_q31, next.width_slow_q31) / 2u);
    wx = comp_triangle_q31(next.phase);
    wy = comp_triangle_q31(next.phase + UINT32_C(0x40000000));
    f.content_offset_x_q31 = comp_scaled_signed(wx, drift_gain);
    f.content_offset_y_q31 = comp_scaled_signed(wy, drift_gain / 2u);

    f.core_scale_q31 = comp_lerp_u31(COMP_Q31_048, COMP_Q31_058,
                                     next.energy_slow_q31);
    f.core_breath_q31 = comp_mul_u31(COMP_Q31_020,
                                     comp_tick_u31(tick->envelope_short_q31));
    f.core_border_q31 = comp_lerp_u31(COMP_Q31_025, COMP_Q31_060,
                                      next.impact_hold_q31);
    f.halo_q31 = comp_add_u31(comp_mul_u31(low, COMP_Q31_080),
                              comp_mul_u31(next.energy_slow_q31, COMP_Q31_025));
    f.radial_gain_q31 = comp_lerp_u31(COMP_Q31_018, COMP_Q31_090,
                                      comp_max_u32(next.energy_slow_q31,
                                                   next.impact_hold_q31));
    f.radial_aperture_q31 = comp_lerp_u31(COMP_Q31_045, COMP_Q31_090,
                                          next.width_slow_q31);
    f.lateral_field_q31 = comp_lerp_u31(COMP_Q31_010, COMP_Q31_080,
                                        next.width_slow_q31);
    f.grid_q31 = comp_lerp_u31(COMP_Q31_005, COMP_Q31_030,
                               next.energy_slow_q31);
    f.particles_q31 = comp_add_u31(comp_mul_u31(next.high_slow_q31, COMP_Q31_090),
                                   comp_mul_u31(next.impact_hold_q31, COMP_Q31_025));
    f.memory_echo_q31 = comp_mul_u31(next.impact_hold_q31, COMP_Q31_070);
    f.fracture_q31 = comp_mul_u31(next.impact_hold_q31,
                                  f.mode == ODM_COMPOSITION_MODE_IMPACT ?
                                  COMP_Q31_090 : COMP_Q31_035);
    f.chroma_split_q31 = comp_mul_u31(f.fracture_q31,
                                      glitch != 0u ? COMP_Q31_090 : COMP_Q31_025);
    f.scan_q31 = comp_lerp_u31(COMP_Q31_005, COMP_Q31_020, next.high_slow_q31);
    f.vignette_q31 = comp_lerp_u31(COMP_Q31_055, COMP_Q31_070,
                                   COMP_Q31_ONE - next.width_slow_q31);
    f.void_q31 = tick->silence != 0u ? COMP_Q31_ONE :
                 comp_mul_u31(COMP_Q31_018, COMP_Q31_ONE - next.energy_slow_q31);
    f.accent_q31 = comp_max_u32(next.impact_hold_q31,
                                comp_tick_u31(tick->envelope_short_q31));

    for (i = 0u; i < ODM_MUSIC_BAND_COUNT; ++i)
        f.band_q31[i] = comp_tick_u31(tick->band_energy_q31[i]);
    for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS; ++i) {
        uint32_t b0 = f.band_q31[i % ODM_MUSIC_BAND_COUNT];
        uint32_t b1 = f.band_q31[(i + 1u) % ODM_MUSIC_BAND_COUNT];
        uint32_t mix = (i & 1u) ? (b0 / 2u + b1 / 2u) : b0;
        uint32_t phase_mod = (uint32_t)((f.ring_phase >> (i % 24u)) & UINT32_C(0xff));
        uint32_t phase_q31 = phase_mod * UINT32_C(8421504); /* <= ~0.999 */
        uint32_t shaped = comp_add_u31(comp_mul_u31(mix, COMP_Q31_080),
                                       comp_mul_u31(next.impact_hold_q31,
                                                    phase_q31 / 5u));
        f.radial_q31[i] = comp_mul_u31(shaped, f.radial_gain_q31);
        f.radial_body_q31[i] = f.radial_q31[i];
        f.radial_release_q31[i] = 0u;
        f.radial_attack_q31[i] = 0u;
    }

    *state = next;
    *out_frame = f;
    return ODM_STATUS_OK;
}

odm_status odm_composition_resolve_music_reaction(
    const odm_music_analysis_tick *base_tick,
    const odm_music_reaction_frame *reaction,
    odm_composition_frame_state *out_frame) {
    odm_composition_frame_state f;
    uint32_t i;
    uint32_t sub, bass, low_mid, body, presence, air, energy;
    uint32_t sub_attack, bass_attack, presence_attack, air_attack;
    uint32_t low_level, low_attack, mid_level, high_attack;
    uint32_t macro_strength, macro_pulse, contextual;
    uint32_t accent_gate, impact_gate, structural_gate, memory_gate;
    if (!base_tick || !reaction || !out_frame) return ODM_STATUS_INVALID_ARGUMENT;
    if (reaction->schema_version != ODM_MUSIC_REACTION_SCHEMA_VERSION)
        return ODM_STATUS_VERSION_MISMATCH;
    if (!comp_reaction_input_valid(base_tick, reaction)) return ODM_STATUS_INVALID_DATA;

    sub = reaction->family_q31[ODM_MUSIC_REACTION_FAMILY_SUB];
    bass = reaction->family_q31[ODM_MUSIC_REACTION_FAMILY_BASS];
    low_mid = reaction->family_q31[ODM_MUSIC_REACTION_FAMILY_LOW_MID];
    body = reaction->family_q31[ODM_MUSIC_REACTION_FAMILY_BODY];
    presence = reaction->family_q31[ODM_MUSIC_REACTION_FAMILY_PRESENCE];
    air = reaction->family_q31[ODM_MUSIC_REACTION_FAMILY_AIR];
    sub_attack = reaction->family_attack_q31[ODM_MUSIC_REACTION_FAMILY_SUB];
    bass_attack = reaction->family_attack_q31[ODM_MUSIC_REACTION_FAMILY_BASS];
    presence_attack = reaction->family_attack_q31[ODM_MUSIC_REACTION_FAMILY_PRESENCE];
    air_attack = reaction->family_attack_q31[ODM_MUSIC_REACTION_FAMILY_AIR];
    contextual = (reaction->frame_flags & ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE) != 0u ? 1u : 0u;
    if ((reaction->frame_flags & ~ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE) != 0u ||
        reaction->macro_salience_q31 > COMP_Q31_ONE || reaction->macro_pulse_q31 > COMP_Q31_ONE ||
        (!contextual && (reaction->macro_salience_q31 != 0u || reaction->macro_pulse_q31 != 0u)))
        return ODM_STATUS_INVALID_DATA;
    macro_strength = contextual ? reaction->macro_salience_q31 : reaction->event_strength_q31;
    macro_pulse = contextual ? reaction->macro_pulse_q31 : reaction->event_pulse_q31;
    /* Full-track contextual streams grant global composition authority only to
     * macro salience. Raw event evidence remains independently observable and
     * cannot directly drag every visual axis. */
    energy = reaction->broadband_q31;
    low_level = comp_max_u32(sub, bass);
    mid_level = comp_max_u32(low_mid, body);
    low_attack = comp_max_u32(sub_attack, bass_attack);
    high_attack = comp_max_u32(presence_attack, air_attack);

    /* Multi-axis salience authority. These are presentation gates, not new
     * audio evidence: modest events may accent, stronger events may drive
     * impact controls, and only exceptional events may drive structural
     * fracture. Streaming uses the same monotone mapping over raw strength. */
    accent_gate = comp_gate_u31(macro_strength, COMP_Q31_020);
    impact_gate = comp_gate_u31(macro_strength, COMP_Q31_055);
    structural_gate = comp_gate_u31(macro_strength, COMP_Q31_080);
    memory_gate = comp_gate_u31(macro_pulse, COMP_Q31_035);

    memset(&f, 0, sizeof(f));
    f.schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
    f.flags = ODM_COMPOSITION_FLAG_RADIAL_HIRES |
              ODM_COMPOSITION_FLAG_STRICT_CAUSAL |
              ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE |
              ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE;
    f.tick_index = base_tick->tick_index;
    f.center_sample = base_tick->center_sample;

    /* Strict causal reaction path: no autonomous project-time motion.
     * Geometry phase is stationary unless a typed music-derived control
     * explicitly changes geometry elsewhere. Legacy Composition retains its
     * procedural phase semantics; Music-Reaction does not inherit them. */
    f.phase = 0u;
    f.ring_phase = 0u;

    if (base_tick->silence != 0u) {
        f.mode = ODM_COMPOSITION_MODE_VOID;
        f.flags |= ODM_COMPOSITION_FLAG_SILENCE;
    } else if (macro_strength >= COMP_Q31_055) {
        f.mode = ODM_COMPOSITION_MODE_IMPACT;
        if (macro_strength >= COMP_Q31_085)
            f.flags |= ODM_COMPOSITION_FLAG_GLITCH_GATE;
    } else if (energy >= COMP_Q31_010) {
        f.mode = ODM_COMPOSITION_MODE_FLOW;
    } else f.mode = ODM_COMPOSITION_MODE_CALM;

    /* Stereo position is signal-derived. There is no autonomous triangle
     * drift in the reaction path. */
    f.content_offset_x_q31 = comp_scaled_signed(base_tick->stereo_balance_q31,
                                                 COMP_Q31_010);
    f.content_offset_y_q31 = 0;

    /* Multi-axis visual causality. Each visual control has a narrow named
     * authority instead of sharing one global impact scalar. Sustained low
     * energy shapes Core scale; low attacks drive breath; body/low-mid drives
     * Grid; presence/air attacks drive particles; only high contextual
     * salience is allowed to fracture the whole scene. */
    f.core_scale_q31 = comp_lerp_u31(COMP_Q31_048, COMP_Q31_058, low_level);
    /* Punch + causal bleed-back. Low-frequency attack owns the fast expansion;
     * contextual pulse memory owns a smaller, slower return tail. */
    f.core_breath_q31 = comp_max_u32(
        comp_mul_u31(low_attack, UINT32_C(751619277)), /* 0.35 */
        comp_mul_u31(memory_gate, UINT32_C(386547057))); /* 0.18 */
    f.core_border_q31 = comp_add_u31(comp_mul_u31(low_attack, COMP_Q31_025),
                                     comp_mul_u31(accent_gate, COMP_Q31_018));

    /* The broad halo responds to actual broadband/high-frequency attack plus
     * bounded macro memory. The 96 local radial amplitudes below remain direct
     * spectral evidence and are never multiplied by macro salience. */
    f.halo_q31 = comp_max_u32(reaction->broadband_attack_q31,
                              comp_add_u31(comp_mul_u31(high_attack, COMP_Q31_035),
                                           comp_mul_u31(memory_gate, COMP_Q31_025)));
    f.radial_gain_q31 = COMP_Q31_ONE;
    f.radial_aperture_q31 = comp_lerp_u31(COMP_Q31_045, COMP_Q31_090,
                                          comp_tick_u31(base_tick->stereo_width_q31));
    f.lateral_field_q31 = comp_lerp_u31(COMP_Q31_010, COMP_Q31_080,
                                        comp_tick_u31(base_tick->stereo_width_q31));
    /* Background is a scene-time-domain response: sustained mid/body energy
     * plus a small contextual-memory contribution. Do not let same-tick attack
     * whip the grid at Halo speed. */
    f.grid_q31 = comp_add_u31(comp_mul_u31(mid_level, UINT32_C(536870912)), /* 0.25 */
                              comp_mul_u31(memory_gate, UINT32_C(171798692))); /* 0.08 */
    f.particles_q31 = high_attack;
    f.memory_echo_q31 = comp_mul_u31(memory_gate, COMP_Q31_055);
    f.fracture_q31 = comp_mul_u31(structural_gate, COMP_Q31_070);
    f.chroma_split_q31 = comp_mul_u31(air_attack, impact_gate);
    f.scan_q31 = comp_mul_u31(air, COMP_Q31_018);
    f.vignette_q31 = comp_lerp_u31(COMP_Q31_055, COMP_Q31_070,
                                   COMP_Q31_ONE - presence);
    f.void_q31 = base_tick->silence != 0u ? COMP_Q31_ONE :
                 comp_mul_u31(COMP_Q31_018, COMP_Q31_ONE - energy);
    f.accent_q31 = comp_max_u32(presence_attack,
                                comp_mul_u31(impact_gate, COMP_Q31_070));

    for (i = 0u; i < ODM_MUSIC_BAND_COUNT; ++i)
        f.band_q31[i] = reaction->family_q31[i];

    /* High-resolution reaction path: 96 spectral causes remain 96 visible
     * radial slots. There is no modulo-six repetition, procedural phase, or
     * pair collapse here. A legacy 48-segment Layered Config may downsample
     * adjacent slots deterministically at the compositor boundary. */
    for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i) {
        /* Multiscale same-lane morphology. Fast envelope owns the immediate
         * sustained body; exact slow-fast excess is release/tail authority;
         * same-tick attack owns only the final remaining headroom. No term can
         * borrow evidence from a neighboring lane or from project time. */
        uint32_t body_gate = comp_soft_knee_u31(reaction->lane_fast_q31[i],
                                                 UINT32_C(107374182),  /* 0.05 */
                                                 UINT32_C(429496729)); /* 0.20 */
        uint32_t attack_gate = comp_soft_knee_u31(reaction->lane_attack_q31[i],
                                                   UINT32_C(171798692),  /* 0.08 */
                                                   UINT32_C(536870912)); /* 0.25 */
        uint32_t radial_body = comp_mul_u31(
            comp_mul_u31(reaction->lane_fast_q31[i], body_gate), COMP_Q31_045);
        uint32_t release = reaction->lane_q31[i] - reaction->lane_fast_q31[i];
        uint32_t release_gate = comp_soft_knee_u31(release,
                                                    UINT32_C(64424509),   /* 0.03 */
                                                    UINT32_C(322122547)); /* 0.15 */
        uint32_t release_mix = comp_mul_u31(
            comp_mul_u31(release, release_gate), COMP_Q31_035);
        uint32_t after_release = comp_add_u31(
            radial_body, comp_mul_u31(COMP_Q31_ONE - radial_body, release_mix));
        uint32_t attack = comp_mul_u31(reaction->lane_attack_q31[i], attack_gate);
        f.radial_body_q31[i] = radial_body;
        /* Provenance exports the perceptually admitted release authority,
         * not the pre-gate residual. This keeps plan validation and raster
         * semantics identical: every published component is exactly what
         * contributes to the visible radial. */
        f.radial_release_q31[i] = comp_mul_u31(release, release_gate);
        f.radial_attack_q31[i] = attack;
        f.radial_q31[i] = comp_add_u31(
            after_release, comp_mul_u31(COMP_Q31_ONE - after_release, attack));
    }

    *out_frame = f;
    return ODM_STATUS_OK;
}

static int comp_projection_zero_u32(const uint32_t *v, uint32_t n) {
    uint32_t i;
    if (!v) return 0;
    for (i = 0u; i < n; ++i) if (v[i] != 0u) return 0;
    return 1;
}

static int comp_projection_valid(const odm_music_reaction_projection *p) {
    const uint32_t allowed_events = ODM_MUSIC_REACTION_EVENT_SUB |
                                    ODM_MUSIC_REACTION_EVENT_BASS |
                                    ODM_MUSIC_REACTION_EVENT_LOW_MID |
                                    ODM_MUSIC_REACTION_EVENT_BODY |
                                    ODM_MUSIC_REACTION_EVENT_PRESENCE |
                                    ODM_MUSIC_REACTION_EVENT_AIR |
                                    ODM_MUSIC_REACTION_EVENT_ANY |
                                    ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
    uint32_t i;
    uint64_t expected_source;
    if (!p || p->schema_version != ODM_MUSIC_REACTION_PROJECTION_SCHEMA_VERSION ||
        p->presentation_sample > (uint64_t)INT64_MAX ||
        p->previous_presentation_sample < -1 ||
        (p->previous_presentation_sample >= 0 &&
         (uint64_t)p->previous_presentation_sample >= p->presentation_sample) ||
        (p->event_flags & ~allowed_events) != 0u ||
        p->captured_event_tick_count > p->captured_tick_count ||
        p->broadband_q31 > (uint32_t)INT32_MAX ||
        p->broadband_attack_q31 > (uint32_t)INT32_MAX ||
        p->event_strength_q31 > (uint32_t)INT32_MAX ||
        p->event_pulse_q31 > (uint32_t)INT32_MAX ||
        p->event_threshold_q31 > (uint32_t)INT32_MAX ||
        p->macro_salience_q31 > (uint32_t)INT32_MAX ||
        p->macro_pulse_q31 > (uint32_t)INT32_MAX ||
        (p->frame_flags & ~ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE) != 0u ||
        !comp_projection_zero_u32(p->reserved, 6u)) return 0;
    if ((p->frame_flags & ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE) == 0u &&
        (p->macro_salience_q31 != 0u || p->macro_pulse_q31 != 0u)) return 0;
    expected_source = p->presentation_sample / (uint64_t)ODM_MUSIC_TICK_SAMPLES;
    if (p->source_tick_index != expected_source ||
        p->source_tick_index > UINT64_MAX / (uint64_t)ODM_MUSIC_TICK_SAMPLES ||
        p->source_center_sample != p->source_tick_index * (uint64_t)ODM_MUSIC_TICK_SAMPLES)
        return 0;
    if (p->captured_tick_count == 0u) {
        if (p->first_captured_tick_index != 0u || p->last_captured_tick_index != 0u ||
            p->event_flags != 0u || p->event_strength_q31 != 0u ||
            p->macro_salience_q31 != 0u || p->broadband_attack_q31 != 0u) return 0;
    } else {
        uint64_t n;
        if (p->last_captured_tick_index < p->first_captured_tick_index) return 0;
        n = p->last_captured_tick_index - p->first_captured_tick_index + 1u;
        if (n != (uint64_t)p->captured_tick_count ||
            p->last_captured_tick_index > p->source_tick_index) return 0;
        if (p->previous_presentation_sample >= 0 &&
            p->first_captured_tick_index * (uint64_t)ODM_MUSIC_TICK_SAMPLES <=
                (uint64_t)p->previous_presentation_sample) return 0;
        if (p->last_captured_tick_index * (uint64_t)ODM_MUSIC_TICK_SAMPLES >
            p->presentation_sample) return 0;
    }
    for (i = 0u; i < ODM_MUSIC_REACTION_LANE_COUNT; ++i) {
        if (p->lane_q31[i] > (uint32_t)INT32_MAX ||
            p->lane_fast_q31[i] > (uint32_t)INT32_MAX ||
            p->lane_attack_q31[i] > (uint32_t)INT32_MAX ||
            p->lane_fast_q31[i] > p->lane_q31[i]) return 0;
        if (p->captured_tick_count == 0u && p->lane_attack_q31[i] != 0u) return 0;
    }
    for (i = 0u; i < ODM_MUSIC_REACTION_FAMILY_COUNT; ++i) {
        if (p->family_q31[i] > (uint32_t)INT32_MAX ||
            p->family_attack_q31[i] > (uint32_t)INT32_MAX) return 0;
        if (p->captured_tick_count == 0u && p->family_attack_q31[i] != 0u) return 0;
    }
    return 1;
}

static int comp_event_projection_valid(
    const odm_music_reaction_projection *reaction_projection,
    const odm_music_reaction_event_projection *event_projection) {
    const uint32_t source_event_mask = ODM_MUSIC_REACTION_EVENT_SUB |
                                       ODM_MUSIC_REACTION_EVENT_BASS |
                                       ODM_MUSIC_REACTION_EVENT_LOW_MID |
                                       ODM_MUSIC_REACTION_EVENT_BODY |
                                       ODM_MUSIC_REACTION_EVENT_PRESENCE |
                                       ODM_MUSIC_REACTION_EVENT_AIR |
                                       ODM_MUSIC_REACTION_EVENT_ANY |
                                       ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
    const uint32_t presentation_mask = source_event_mask |
                                       ODM_MUSIC_REACTION_EVENT_SUBTICK_TRUSTED;
    if (!reaction_projection || !event_projection ||
        event_projection->schema_version != ODM_MUSIC_REACTION_EVENT_PROJECTION_SCHEMA_VERSION ||
        event_projection->previous_presentation_sample != reaction_projection->previous_presentation_sample ||
        event_projection->presentation_sample != reaction_projection->presentation_sample ||
        event_projection->presentation_sample > (uint64_t)INT64_MAX ||
        (event_projection->event_flags & ~presentation_mask) != 0u ||
        event_projection->event_salience_q31 > COMP_Q31_ONE ||
        event_projection->event_pulse_q31 > COMP_Q31_ONE ||
        event_projection->timing_confidence_q31 > COMP_Q31_ONE ||
        event_projection->broadband_attack_q31 > COMP_Q31_ONE ||
        event_projection->score_q31 > COMP_Q31_ONE ||
        event_projection->strength_basis_q31 > COMP_Q31_ONE ||
        event_projection->pulse_owner_salience_q31 > COMP_Q31_ONE ||
        event_projection->dominant_pulse_q31 > COMP_Q31_ONE ||
        !comp_zero_words(event_projection->reserved, 3u)) return 0;

    if (event_projection->captured_event_count == 0u) {
        if (event_projection->first_event_index != UINT64_MAX ||
            event_projection->last_event_index != UINT64_MAX ||
            event_projection->dominant_event_index != UINT64_MAX ||
            event_projection->dominant_effective_sample != UINT64_MAX ||
            event_projection->event_salience_q31 != 0u ||
            event_projection->timing_confidence_q31 != 0u ||
            event_projection->strongest_family != UINT32_MAX ||
            event_projection->second_family != UINT32_MAX ||
            event_projection->broadband_attack_q31 != 0u ||
            event_projection->score_q31 != 0u ||
            event_projection->strength_basis_q31 != 0u ||
            event_projection->dominant_pulse_q31 != 0u ||
            (event_projection->event_flags & source_event_mask) != 0u ||
            (event_projection->event_flags & ODM_MUSIC_REACTION_EVENT_SUBTICK_TRUSTED) != 0u)
            return 0;
    } else {
        if (event_projection->first_event_index == UINT64_MAX ||
            event_projection->last_event_index == UINT64_MAX ||
            event_projection->dominant_event_index == UINT64_MAX ||
            event_projection->dominant_effective_sample == UINT64_MAX ||
            event_projection->first_event_index > event_projection->last_event_index ||
            event_projection->dominant_event_index < event_projection->first_event_index ||
            event_projection->dominant_event_index > event_projection->last_event_index ||
            event_projection->dominant_effective_sample > event_projection->presentation_sample ||
            event_projection->event_salience_q31 == 0u ||
            event_projection->dominant_pulse_q31 == 0u ||
            event_projection->dominant_pulse_q31 > event_projection->event_salience_q31 ||
            event_projection->strength_basis_q31 == 0u ||
            (event_projection->event_flags & ODM_MUSIC_REACTION_EVENT_ANY) == 0u ||
            event_projection->strongest_family >= ODM_MUSIC_REACTION_FAMILY_COUNT ||
            event_projection->second_family >= ODM_MUSIC_REACTION_FAMILY_COUNT ||
            event_projection->strongest_family == event_projection->second_family)
            return 0;
        if ((event_projection->event_flags & ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK) != 0u &&
            (event_projection->event_flags & ODM_MUSIC_REACTION_EVENT_ANY) == 0u) return 0;
    }

    if (event_projection->event_pulse_q31 == 0u) {
        if (event_projection->pulse_owner_event_index != UINT64_MAX ||
            event_projection->pulse_owner_effective_sample != UINT64_MAX ||
            event_projection->pulse_owner_salience_q31 != 0u) return 0;
    } else {
        if (event_projection->pulse_owner_event_index == UINT64_MAX ||
            event_projection->pulse_owner_effective_sample == UINT64_MAX ||
            event_projection->pulse_owner_effective_sample > event_projection->presentation_sample ||
            event_projection->pulse_owner_salience_q31 == 0u ||
            event_projection->event_pulse_q31 > event_projection->pulse_owner_salience_q31)
            return 0;
    }
    return 1;
}

odm_status odm_composition_resolve_music_reaction_projection(
    const odm_music_analysis_tick *base_tick,
    const odm_music_reaction_projection *projection,
    odm_composition_frame_state *out_frame) {
    odm_music_reaction_frame r;
    if (!base_tick || !projection || !out_frame) return ODM_STATUS_INVALID_ARGUMENT;
    if (!comp_projection_valid(projection)) return ODM_STATUS_INVALID_DATA;
    if (base_tick->tick_index != projection->source_tick_index ||
        base_tick->center_sample != projection->source_center_sample)
        return ODM_STATUS_INVALID_DATA;
    memset(&r, 0, sizeof(r));
    r.schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    r.event_flags = projection->event_flags;
    r.tick_index = projection->source_tick_index;
    r.center_sample = projection->source_center_sample;
    memcpy(r.lane_q31, projection->lane_q31, sizeof(r.lane_q31));
    memcpy(r.lane_fast_q31, projection->lane_fast_q31, sizeof(r.lane_fast_q31));
    memcpy(r.lane_attack_q31, projection->lane_attack_q31, sizeof(r.lane_attack_q31));
    memcpy(r.family_q31, projection->family_q31, sizeof(r.family_q31));
    memcpy(r.family_attack_q31, projection->family_attack_q31, sizeof(r.family_attack_q31));
    r.broadband_q31 = projection->broadband_q31;
    r.broadband_attack_q31 = projection->broadband_attack_q31;
    r.event_strength_q31 = projection->event_strength_q31;
    r.event_pulse_q31 = projection->event_pulse_q31;
    r.event_threshold_q31 = projection->event_threshold_q31;
    r.macro_salience_q31 = projection->macro_salience_q31;
    r.macro_pulse_q31 = projection->macro_pulse_q31;
    r.frame_flags = projection->frame_flags;
    return odm_composition_resolve_music_reaction(base_tick, &r, out_frame);
}

odm_status odm_composition_resolve_music_reaction_sample_presentation(
    const odm_music_analysis_tick *base_tick,
    const odm_music_reaction_projection *projection,
    const odm_music_reaction_event_projection *event_projection,
    odm_composition_frame_state *out_frame) {
    const uint32_t source_event_mask = ODM_MUSIC_REACTION_EVENT_SUB |
                                       ODM_MUSIC_REACTION_EVENT_BASS |
                                       ODM_MUSIC_REACTION_EVENT_LOW_MID |
                                       ODM_MUSIC_REACTION_EVENT_BODY |
                                       ODM_MUSIC_REACTION_EVENT_PRESENCE |
                                       ODM_MUSIC_REACTION_EVENT_AIR |
                                       ODM_MUSIC_REACTION_EVENT_ANY |
                                       ODM_MUSIC_REACTION_EVENT_OFFLINE_PEAK;
    odm_music_reaction_frame r;
    if (!base_tick || !projection || !event_projection || !out_frame)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (!comp_projection_valid(projection) ||
        !comp_event_projection_valid(projection, event_projection))
        return ODM_STATUS_INVALID_DATA;
    if (base_tick->tick_index != projection->source_tick_index ||
        base_tick->center_sample != projection->source_center_sample)
        return ODM_STATUS_INVALID_DATA;

    memset(&r, 0, sizeof(r));
    r.schema_version = ODM_MUSIC_REACTION_SCHEMA_VERSION;
    r.tick_index = projection->source_tick_index;
    r.center_sample = projection->source_center_sample;
    memcpy(r.lane_q31, projection->lane_q31, sizeof(r.lane_q31));
    memcpy(r.lane_fast_q31, projection->lane_fast_q31, sizeof(r.lane_fast_q31));
    memcpy(r.lane_attack_q31, projection->lane_attack_q31, sizeof(r.lane_attack_q31));
    memcpy(r.family_q31, projection->family_q31, sizeof(r.family_q31));
    memcpy(r.family_attack_q31, projection->family_attack_q31, sizeof(r.family_attack_q31));
    r.broadband_q31 = projection->broadband_q31;
    r.broadband_attack_q31 = projection->broadband_attack_q31;
    r.event_threshold_q31 = projection->event_threshold_q31;

    /* The causal 100 Hz projection continues to own local spectral morphology.
     * Global event authority, however, is deliberately replaced by the sparse
     * sample-domain event projection so one event cannot appear once at its
     * refined sample and a second time at its coarse tick center. */
    r.event_flags = event_projection->event_flags & source_event_mask;
    r.event_strength_q31 = event_projection->captured_event_count != 0u
                               ? event_projection->strength_basis_q31 : 0u;
    r.event_pulse_q31 = 0u;
    /* Presentation-domain closure. Provenance keeps the original event
     * salience in event_projection; the raster receives only the amount of
     * that event still alive at this exact video-frame sample. At frame rates
     * above the 100 Hz analysis grid it is valid for interval-max salience to
     * exceed the held pulse, so do not reclassify that pair as a native frame. */
    if (event_projection->captured_event_count != 0u &&
        event_projection->event_pulse_q31 != 0u) {
        r.macro_salience_q31 = event_projection->dominant_pulse_q31 <
                               event_projection->event_pulse_q31
                                   ? event_projection->dominant_pulse_q31
                                   : event_projection->event_pulse_q31;
    } else r.macro_salience_q31 = 0u;
    r.macro_pulse_q31 = event_projection->event_pulse_q31;
    r.frame_flags = ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE;
    return odm_composition_resolve_music_reaction(base_tick, &r, out_frame);
}


odm_status odm_composition_resolve_batch(odm_composition_resolver_state *state,
                                         const odm_music_analysis_tick *ticks,
                                         uint64_t tick_count,
                                         odm_composition_frame_state *out_frames,
                                         uint64_t frame_capacity) {
    odm_composition_resolver_state tmp;
    uint64_t i, expected;
    odm_status st;
    if (!state) return ODM_STATUS_INVALID_ARGUMENT;
    if (state->schema_version != ODM_COMPOSITION_SCHEMA_VERSION)
        return ODM_STATUS_VERSION_MISMATCH;
    if (tick_count == 0u) return ODM_STATUS_OK;
    if (!ticks || !out_frames) return ODM_STATUS_INVALID_ARGUMENT;
    if (frame_capacity < tick_count) return ODM_STATUS_BUFFER_TOO_SMALL;

    /* Preflight the complete sequence before touching caller output. */
    expected = state->initialized != 0u ? state->next_tick_index : ticks[0].tick_index;
    for (i = 0u; i < tick_count; ++i) {
        if (ticks[i].tick_index != expected || ticks[i].silence > 1u)
            return ODM_STATUS_INVALID_DATA;
        if (expected == UINT64_MAX) return ODM_STATUS_OVERFLOW;
        ++expected;
    }

    tmp = *state;
    for (i = 0u; i < tick_count; ++i) {
        st = odm_composition_resolve_tick(&tmp, &ticks[i], &out_frames[i]);
        if (st != ODM_STATUS_OK) return ODM_STATUS_INVARIANT_BROKEN;
    }
    *state = tmp;
    return ODM_STATUS_OK;
}

static uint32_t comp_norm_u31(uint32_t value, uint32_t reference) {
    uint64_t num;
    if (reference == 0u || value == 0u) return 0u;
    if (value >= reference) return COMP_Q31_ONE;
    num = (uint64_t)value * (uint64_t)COMP_Q31_ONE;
    num += (uint64_t)reference / 2u;
    return comp_sat_u31(num / (uint64_t)reference);
}

static uint32_t comp_norm_delta_u31(int64_t value, uint64_t reference) {
    uint64_t u, num;
    if (value <= 0 || reference == 0u) return 0u;
    u = (uint64_t)value;
    if (u >= reference) return COMP_Q31_ONE;
    if (u > UINT64_MAX / (uint64_t)COMP_Q31_ONE) return COMP_Q31_ONE;
    num = u * (uint64_t)COMP_Q31_ONE;
    num += reference / 2u;
    return comp_sat_u31(num / reference);
}

odm_status odm_composition_profile_build(const odm_music_analysis_tick *ticks,
                                         uint64_t tick_count,
                                         odm_composition_profile *out_profile) {
    odm_composition_profile p;
    uint64_t i, expected;
    uint32_t b;
    if (!ticks || !out_profile || tick_count == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    memset(&p, 0, sizeof(p));
    p.schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
    p.tick_count = tick_count;
    p.first_tick_index = ticks[0].tick_index;
    expected = ticks[0].tick_index;
    for (i = 0u; i < tick_count; ++i) {
        const odm_music_analysis_tick *t = &ticks[i];
        uint64_t pd = t->energy_delta_q31 > 0 ? (uint64_t)t->energy_delta_q31 : 0u;
        if (t->tick_index != expected || t->tick_index == UINT64_MAX || t->silence > 1u)
            return t->tick_index == UINT64_MAX ? ODM_STATUS_OVERFLOW : ODM_STATUS_INVALID_DATA;
        ++expected;
        if (t->envelope_short_q31 > p.envelope_short_ref) p.envelope_short_ref = t->envelope_short_q31;
        if (t->envelope_medium_q31 > p.envelope_medium_ref) p.envelope_medium_ref = t->envelope_medium_q31;
        if (t->spectral_flux_q31 > p.spectral_flux_ref) p.spectral_flux_ref = t->spectral_flux_q31;
        if (pd > p.positive_delta_ref) p.positive_delta_ref = pd;
        for (b = 0u; b < ODM_MUSIC_BAND_COUNT; ++b)
            if (t->band_energy_q31[b] > p.band_ref[b]) p.band_ref[b] = t->band_energy_q31[b];
    }
    p.last_tick_index = ticks[tick_count - 1u].tick_index;
    /* Zero-reference features are valid (e.g. a silent/high-band-empty file).
     * Normalization maps them to zero rather than inventing energy. */
    *out_profile = p;
    return ODM_STATUS_OK;
}

odm_status odm_composition_resolve_tick_profiled(odm_composition_resolver_state *state,
                                                  const odm_composition_profile *profile,
                                                  const odm_music_analysis_tick *tick,
                                                  odm_composition_frame_state *out_frame) {
    odm_music_analysis_tick n;
    uint32_t b;
    if (!state || !profile || !tick || !out_frame) return ODM_STATUS_INVALID_ARGUMENT;
    if (profile->schema_version != ODM_COMPOSITION_SCHEMA_VERSION)
        return ODM_STATUS_VERSION_MISMATCH;
    if (tick->tick_index < profile->first_tick_index || tick->tick_index > profile->last_tick_index)
        return ODM_STATUS_INVALID_DATA;
    n = *tick;
    n.envelope_short_q31 = comp_norm_u31(tick->envelope_short_q31, profile->envelope_short_ref);
    n.envelope_medium_q31 = comp_norm_u31(tick->envelope_medium_q31, profile->envelope_medium_ref);
    {
        uint32_t q = comp_norm_u31(tick->spectral_flux_q31, profile->spectral_flux_ref);
        n.spectral_flux_q31 = comp_mul_u31(comp_mul_u31(q, q), q);
    }
    {
        uint32_t q = comp_norm_delta_u31(tick->energy_delta_q31, profile->positive_delta_ref);
        n.energy_delta_q31 = (int64_t)comp_mul_u31(comp_mul_u31(q, q), q);
    }
    for (b = 0u; b < ODM_MUSIC_BAND_COUNT; ++b)
        n.band_energy_q31[b] = comp_norm_u31(tick->band_energy_q31[b], profile->band_ref[b]);
    return odm_composition_resolve_tick(state, &n, out_frame);
}

odm_status odm_composition_resolve_batch_profiled(odm_composition_resolver_state *state,
                                                   const odm_composition_profile *profile,
                                                   const odm_music_analysis_tick *ticks,
                                                   uint64_t tick_count,
                                                   odm_composition_frame_state *out_frames,
                                                   uint64_t frame_capacity) {
    odm_composition_resolver_state tmp;
    uint64_t i, expected;
    odm_status st;
    if (!state || !profile) return ODM_STATUS_INVALID_ARGUMENT;
    if (profile->schema_version != ODM_COMPOSITION_SCHEMA_VERSION)
        return ODM_STATUS_VERSION_MISMATCH;
    if (tick_count == 0u) return ODM_STATUS_OK;
    if (!ticks || !out_frames) return ODM_STATUS_INVALID_ARGUMENT;
    if (frame_capacity < tick_count) return ODM_STATUS_BUFFER_TOO_SMALL;
    expected = state->initialized != 0u ? state->next_tick_index : ticks[0].tick_index;
    for (i = 0u; i < tick_count; ++i) {
        if (ticks[i].tick_index != expected || ticks[i].tick_index == UINT64_MAX || ticks[i].silence > 1u ||
            ticks[i].tick_index < profile->first_tick_index || ticks[i].tick_index > profile->last_tick_index)
            return ticks[i].tick_index == UINT64_MAX ? ODM_STATUS_OVERFLOW : ODM_STATUS_INVALID_DATA;
        ++expected;
    }
    tmp = *state;
    for (i = 0u; i < tick_count; ++i) {
        st = odm_composition_resolve_tick_profiled(&tmp, profile, &ticks[i], &out_frames[i]);
        if (st != ODM_STATUS_OK) return ODM_STATUS_INVARIANT_BROKEN;
    }
    *state = tmp;
    return ODM_STATUS_OK;
}

/* ================= Visual Director v3 =================
 *
 * Macro-composition is intentionally downstream of Composition v1.  It
 * consumes only normalized composition state and therefore cannot alter the
 * canonical Music Map or its policy hash.  All transitions are deterministic,
 * allocation-free and O(1) per tick.
 */

#define DIR_Q31_002 UINT32_C(42949673)
#define DIR_Q31_004 UINT32_C(85899346)
#define DIR_Q31_006 UINT32_C(128849019)
#define DIR_Q31_008 UINT32_C(171798692)
#define DIR_Q31_012 UINT32_C(257698038)
#define DIR_Q31_014 UINT32_C(300647711)
#define DIR_Q31_016 UINT32_C(343597384)
#define DIR_Q31_022 UINT32_C(472446402)
#define DIR_Q31_028 UINT32_C(601295421)
#define DIR_Q31_032 UINT32_C(687194767)
#define DIR_Q31_038 UINT32_C(816043786)
#define DIR_Q31_040 UINT32_C(858993459)
#define DIR_Q31_042 UINT32_C(901943132)
#define DIR_Q31_044 UINT32_C(944892805)
#define DIR_Q31_046 UINT32_C(987842478)
#define DIR_Q31_052 UINT32_C(1116691497)
#define DIR_Q31_056 UINT32_C(1202590842)
#define DIR_Q31_062 UINT32_C(1331439861)
#define DIR_Q31_068 UINT32_C(1460288880)
#define DIR_Q31_072 UINT32_C(1546188226)
#define DIR_Q31_075 UINT32_C(1610612735)
#define DIR_Q31_078 UINT32_C(1675037245)
#define DIR_Q31_085 UINT32_C(1825361100)
#define DIR_Q31_088 UINT32_C(1889785610)
#define DIR_Q31_092 UINT32_C(1975684955)
#define DIR_Q31_095 UINT32_C(2040109465)
#define DIR_Q31_098 UINT32_C(2104533974)
#define DIR_MIN_DWELL_TICKS UINT32_C(1200)
#define DIR_CANDIDATE_STABLE_TICKS UINT32_C(180)
#define DIR_FRACTURE_STABLE_TICKS UINT32_C(10)
#define DIR_FRACTURE_DWELL_TICKS UINT32_C(1200)
#define DIR_FRACTURE_RELEASE_STABLE_TICKS UINT32_C(80)
#define DIR_FRACTURE_MIN_HOLD_TICKS UINT32_C(180)
#define DIR_VOID_STABLE_TICKS UINT32_C(20)
#define DIR_RECOVERY_STABLE_TICKS UINT32_C(10)
#define DIR_STAGNATION_DWELL_TICKS UINT32_C(2800)
#define DIR_TRANSITION_TICKS UINT32_C(180)
#define DIR_FAST_TRANSITION_TICKS UINT32_C(70)
#define DIR_NOVELTY_THRESHOLD (UINT64_C(180) * UINT64_C(2147483647))
#define DIR_STAGNATION_THRESHOLD (UINT64_C(550) * UINT64_C(2147483647))

typedef struct {
    uint32_t core;
    uint32_t orbit;
    uint32_t wings;
    uint32_t memory;
    uint32_t tunnel;
    uint32_t architecture;
    uint32_t backdrop;
    uint32_t depth;
    uint32_t edge;
    uint32_t layers;
    uint32_t panes;
} odm_dir_targets;

static uint32_t dir_norm_range_u31(uint32_t value, uint32_t lo, uint32_t hi) {
    uint64_t num;
    uint32_t span;
    if (hi <= lo || value <= lo) return 0u;
    if (value >= hi) return COMP_Q31_ONE;
    span = hi - lo;
    num = (uint64_t)(value - lo) * (uint64_t)COMP_Q31_ONE;
    num += (uint64_t)span / 2u;
    return comp_sat_u31(num / (uint64_t)span);
}

static uint32_t dir_radial_activity_u31(const odm_composition_frame_state *c) {
    uint64_t sum = 0u;
    uint32_t i;
    uint32_t count;
    if (!c) return 0u;
    count = (c->flags & ODM_COMPOSITION_FLAG_RADIAL_HIRES) != 0u ?
            ODM_COMPOSITION_RADIAL_SEGMENTS_MAX : ODM_COMPOSITION_RADIAL_SEGMENTS;
    for (i = 0u; i < count; ++i) sum += (uint64_t)c->radial_q31[i];
    return (uint32_t)((sum + (uint64_t)count / 2u) / (uint64_t)count);
}

static uint32_t dir_hash32(uint64_t seed, uint32_t epoch) {
    uint64_t z = seed + UINT64_C(0x9e3779b97f4a7c15) * (uint64_t)(epoch + 1u);
    z ^= z >> 30u;
    z *= UINT64_C(0xbf58476d1ce4e5b9);
    z ^= z >> 27u;
    z *= UINT64_C(0x94d049bb133111eb);
    z ^= z >> 31u;
    return (uint32_t)(z ^ (z >> 32u));
}

static odm_dir_targets dir_targets(uint32_t layout) {
    odm_dir_targets t;
    memset(&t, 0, sizeof(t));
    switch (layout) {
      case ODM_DIRECTOR_LAYOUT_MONOLITH:
        t.core=DIR_Q31_056;t.orbit=COMP_Q31_015;t.wings=COMP_Q31_012;t.memory=DIR_Q31_008;
        t.tunnel=COMP_Q31_020;t.architecture=COMP_Q31_035;t.backdrop=COMP_Q31_015;t.depth=COMP_Q31_020;t.edge=COMP_Q31_025;t.layers=2u;t.panes=0u;break;
      case ODM_DIRECTOR_LAYOUT_ORBIT:
        t.core=COMP_Q31_048;t.orbit=DIR_Q31_088;t.wings=COMP_Q31_020;t.memory=COMP_Q31_012;
        t.tunnel=COMP_Q31_035;t.architecture=COMP_Q31_060;t.backdrop=COMP_Q31_020;t.depth=COMP_Q31_045;t.edge=COMP_Q31_060;t.layers=4u;t.panes=0u;break;
      case ODM_DIRECTOR_LAYOUT_WINGS:
        t.core=DIR_Q31_046;t.orbit=COMP_Q31_035;t.wings=DIR_Q31_092;t.memory=COMP_Q31_018;
        t.tunnel=COMP_Q31_030;t.architecture=DIR_Q31_052;t.backdrop=DIR_Q31_022;t.depth=COMP_Q31_050;t.edge=DIR_Q31_078;t.layers=3u;t.panes=2u;break;
      case ODM_DIRECTOR_LAYOUT_MEMORY:
        t.core=DIR_Q31_044;t.orbit=DIR_Q31_028;t.wings=COMP_Q31_025;t.memory=DIR_Q31_092;
        t.tunnel=COMP_Q31_055;t.architecture=COMP_Q31_065;t.backdrop=COMP_Q31_030;t.depth=DIR_Q31_072;t.edge=COMP_Q31_055;t.layers=3u;t.panes=4u;break;
      case ODM_DIRECTOR_LAYOUT_EXPAND:
        t.core=DIR_Q31_040;t.orbit=DIR_Q31_072;t.wings=COMP_Q31_070;t.memory=COMP_Q31_030;
        t.tunnel=DIR_Q31_092;t.architecture=DIR_Q31_085;t.backdrop=DIR_Q31_038;t.depth=COMP_Q31_080;t.edge=DIR_Q31_085;t.layers=5u;t.panes=2u;break;
      case ODM_DIRECTOR_LAYOUT_VOID:
        t.core=DIR_Q31_052;t.orbit=COMP_Q31_005;t.wings=DIR_Q31_002;t.memory=COMP_Q31_020;
        t.tunnel=DIR_Q31_008;t.architecture=COMP_Q31_015;t.backdrop=DIR_Q31_002;t.depth=COMP_Q31_012;t.edge=COMP_Q31_005;t.layers=1u;t.panes=0u;break;
      case ODM_DIRECTOR_LAYOUT_FRACTURE:
      default:
        t.core=COMP_Q31_045;t.orbit=DIR_Q31_052;t.wings=DIR_Q31_062;t.memory=DIR_Q31_068;
        t.tunnel=COMP_Q31_045;t.architecture=DIR_Q31_078;t.backdrop=DIR_Q31_028;t.depth=COMP_Q31_060;t.edge=COMP_Q31_ONE;t.layers=4u;t.panes=3u;break;
    }
    return t;
}

static uint32_t dir_candidate(const odm_composition_frame_state *c,
                              uint32_t energy, uint32_t width,
                              uint32_t memory, uint32_t fracture,
                              uint32_t radial_activity) {
    if (c->mode == ODM_COMPOSITION_MODE_VOID || c->void_q31 >= COMP_Q31_065)
        return ODM_DIRECTOR_LAYOUT_VOID;
    if ((c->flags & ODM_COMPOSITION_FLAG_GLITCH_GATE) != 0u || fracture >= COMP_Q31_045)
        return ODM_DIRECTOR_LAYOUT_FRACTURE;
    if (memory >= COMP_Q31_035) return ODM_DIRECTOR_LAYOUT_MEMORY;
    if (width >= COMP_Q31_055) return ODM_DIRECTOR_LAYOUT_WINGS;
    if (energy >= COMP_Q31_065) return ODM_DIRECTOR_LAYOUT_EXPAND;
    if (radial_activity >= COMP_Q31_055 || c->halo_q31 >= COMP_Q31_050)
        return ODM_DIRECTOR_LAYOUT_ORBIT;
    return ODM_DIRECTOR_LAYOUT_MONOLITH;
}

static uint32_t dir_alternate(uint32_t current, uint64_t seed, uint32_t epoch) {
    static const uint32_t choices[5] = {
        ODM_DIRECTOR_LAYOUT_MONOLITH, ODM_DIRECTOR_LAYOUT_ORBIT,
        ODM_DIRECTOR_LAYOUT_WINGS, ODM_DIRECTOR_LAYOUT_MEMORY,
        ODM_DIRECTOR_LAYOUT_EXPAND
    };
    uint32_t h = dir_hash32(seed, epoch);
    uint32_t i = h % 5u;
    uint32_t k;
    for (k = 0u; k < 5u; ++k) {
        uint32_t v = choices[(i + k) % 5u];
        if (v != current) return v;
    }
    return ODM_DIRECTOR_LAYOUT_ORBIT;
}

static uint32_t dir_blend(uint32_t a, uint32_t b, uint32_t t) {
    if (a <= b) return comp_lerp_u31(a, b, t);
    return a - comp_mul_u31(a - b, t);
}

odm_status odm_director_init(odm_director_state *state, uint64_t seed) {
    if (!state) return ODM_STATUS_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    state->schema_version = ODM_DIRECTOR_SCHEMA_VERSION;
    state->seed = seed;
    state->phase = (uint32_t)(seed ^ (seed >> 32u) ^ UINT64_C(0xa5a5a5a5));
    state->current_layout = ODM_DIRECTOR_LAYOUT_MONOLITH;
    state->previous_layout = ODM_DIRECTOR_LAYOUT_MONOLITH;
    state->candidate_layout = ODM_DIRECTOR_LAYOUT_MONOLITH;
    return ODM_STATUS_OK;
}

odm_status odm_director_resolve_tick(odm_director_state *state,
                                     const odm_composition_frame_state *c,
                                     odm_director_frame_state *out_frame) {
    odm_director_state n;
    odm_director_frame_state f;
    odm_dir_targets a, b;
    uint32_t energy, width, memory, fracture, novelty, desired, radial_activity;
    uint32_t shifted = 0u, recovery = 0u, impact_flag = 0u;
    uint32_t transition, activity, asym_gain, phase_step;
    uint32_t strict_reaction;
    int32_t wx, wy;

    if (!state || !c || !out_frame) return ODM_STATUS_INVALID_ARGUMENT;
    if (state->schema_version != ODM_DIRECTOR_SCHEMA_VERSION ||
        c->schema_version != ODM_COMPOSITION_SCHEMA_VERSION)
        return ODM_STATUS_VERSION_MISMATCH;
    if (c->mode > ODM_COMPOSITION_MODE_IMPACT) return ODM_STATUS_INVALID_DATA;
    if (c->tick_index == UINT64_MAX) return ODM_STATUS_OVERFLOW;
    if (state->initialized != 0u && c->tick_index != state->next_tick_index)
        return ODM_STATUS_INVALID_DATA;

    strict_reaction = (c->flags & ODM_COMPOSITION_FLAG_STRICT_CAUSAL) != 0u ? 1u : 0u;
    /* radial_gain is a transfer coefficient, not measured activity. In the
     * strict Music-Reaction path it is unity, so using it as Director evidence
     * would fabricate permanent radial activity. Derive macro radial activity
     * from the visible 48/96-sector field instead; legacy retains its historic
     * transfer-gain semantics. */
    radial_activity = strict_reaction != 0u ? dir_radial_activity_u31(c) : c->radial_gain_q31;
    energy = dir_norm_range_u31(c->core_scale_q31, COMP_Q31_048, COMP_Q31_058);
    width = dir_norm_range_u31(c->lateral_field_q31, COMP_Q31_010, COMP_Q31_080);
    memory = c->memory_echo_q31 >= COMP_Q31_070 ? COMP_Q31_ONE :
             comp_norm_u31(c->memory_echo_q31, COMP_Q31_070);
    fracture = c->fracture_q31 >= COMP_Q31_090 ? COMP_Q31_ONE :
               comp_norm_u31(c->fracture_q31, COMP_Q31_090);
    novelty = comp_max_u32(c->accent_q31, fracture);
    if ((c->flags & ODM_COMPOSITION_FLAG_GLITCH_GATE) != 0u) novelty = COMP_Q31_ONE;
    impact_flag = (c->mode == ODM_COMPOSITION_MODE_IMPACT ||
                   (c->flags & ODM_COMPOSITION_FLAG_GLITCH_GATE) != 0u) ? 1u : 0u;

    n = *state;
    if (n.initialized == 0u) {
        desired = dir_candidate(c, energy, width, memory, fracture, radial_activity);
        n.initialized = 1u;
        n.current_layout = desired;
        n.previous_layout = desired;
        n.candidate_layout = desired;
        n.candidate_ticks = 1u;
        n.dwell_ticks = 1u;
        n.macro_energy_q31 = energy;
        n.macro_width_q31 = width;
        n.macro_memory_q31 = memory;
        n.macro_fracture_q31 = fracture;
    } else {
        n.macro_energy_q31 = comp_smooth_u31(n.macro_energy_q31, energy, 5u);
        n.macro_width_q31 = comp_smooth_u31(n.macro_width_q31, width, 5u);
        n.macro_memory_q31 = comp_smooth_u31(n.macro_memory_q31, memory, 4u);
        n.macro_fracture_q31 = comp_smooth_u31(n.macro_fracture_q31, fracture, 3u);
        if (n.dwell_ticks != UINT32_MAX) ++n.dwell_ticks;
        if (UINT64_MAX - n.novelty_accum_q31_ticks < (uint64_t)novelty)
            n.novelty_accum_q31_ticks = UINT64_MAX;
        else n.novelty_accum_q31_ticks += (uint64_t)novelty;

        desired = dir_candidate(c, n.macro_energy_q31, n.macro_width_q31,
                                n.macro_memory_q31, n.macro_fracture_q31, radial_activity);
        if (desired == n.candidate_layout) {
            if (n.candidate_ticks != UINT32_MAX) ++n.candidate_ticks;
        } else {
            n.candidate_layout = desired;
            n.candidate_ticks = 1u;
        }

        if (n.candidate_layout == ODM_DIRECTOR_LAYOUT_VOID &&
            n.current_layout != ODM_DIRECTOR_LAYOUT_VOID &&
            n.candidate_ticks >= DIR_VOID_STABLE_TICKS && n.dwell_ticks >= DIR_VOID_STABLE_TICKS) {
            n.previous_layout = n.current_layout; n.current_layout = ODM_DIRECTOR_LAYOUT_VOID;
            n.transition_ticks_left = DIR_FAST_TRANSITION_TICKS; n.transition_total_ticks = DIR_FAST_TRANSITION_TICKS; n.dwell_ticks = 0u;
            n.novelty_accum_q31_ticks = 0u; ++n.layout_epoch; shifted = 1u;
        } else if (n.current_layout == ODM_DIRECTOR_LAYOUT_VOID &&
                   n.candidate_layout != ODM_DIRECTOR_LAYOUT_VOID &&
                   n.candidate_ticks >= DIR_RECOVERY_STABLE_TICKS && n.macro_energy_q31 >= COMP_Q31_012) {
            n.previous_layout = n.current_layout; n.current_layout = ODM_DIRECTOR_LAYOUT_EXPAND;
            n.transition_ticks_left = DIR_FAST_TRANSITION_TICKS; n.transition_total_ticks = DIR_FAST_TRANSITION_TICKS; n.dwell_ticks = 0u;
            n.novelty_accum_q31_ticks = 0u; ++n.layout_epoch; shifted = 1u; recovery = 1u;
        } else if (n.current_layout == ODM_DIRECTOR_LAYOUT_FRACTURE &&
                   n.candidate_layout != ODM_DIRECTOR_LAYOUT_FRACTURE &&
                   n.candidate_ticks >= DIR_FRACTURE_RELEASE_STABLE_TICKS &&
                   n.dwell_ticks >= DIR_FRACTURE_MIN_HOLD_TICKS) {
            n.previous_layout = n.current_layout; n.current_layout = n.candidate_layout;
            n.transition_ticks_left = DIR_FAST_TRANSITION_TICKS; n.transition_total_ticks = DIR_FAST_TRANSITION_TICKS; n.dwell_ticks = 0u;
            n.novelty_accum_q31_ticks = 0u; ++n.layout_epoch; shifted = 1u;
        } else if (n.candidate_layout == ODM_DIRECTOR_LAYOUT_FRACTURE &&
                   n.current_layout != ODM_DIRECTOR_LAYOUT_FRACTURE &&
                   n.candidate_ticks >= DIR_FRACTURE_STABLE_TICKS &&
                   n.dwell_ticks >= DIR_FRACTURE_DWELL_TICKS &&
                   c->fracture_q31 >= DIR_Q31_040) {
            n.previous_layout = n.current_layout; n.current_layout = ODM_DIRECTOR_LAYOUT_FRACTURE;
            n.transition_ticks_left = DIR_FAST_TRANSITION_TICKS; n.transition_total_ticks = DIR_FAST_TRANSITION_TICKS; n.dwell_ticks = 0u;
            n.novelty_accum_q31_ticks = 0u; ++n.layout_epoch; shifted = 1u;
        } else if (n.candidate_layout != n.current_layout &&
                   n.candidate_ticks >= DIR_CANDIDATE_STABLE_TICKS &&
                   n.dwell_ticks >= DIR_MIN_DWELL_TICKS &&
                   n.novelty_accum_q31_ticks >= DIR_NOVELTY_THRESHOLD) {
            n.previous_layout = n.current_layout; n.current_layout = n.candidate_layout;
            n.transition_ticks_left = DIR_TRANSITION_TICKS; n.transition_total_ticks = DIR_TRANSITION_TICKS;
            n.dwell_ticks = 0u; n.novelty_accum_q31_ticks = 0u; ++n.layout_epoch; shifted = 1u;
        } else if (strict_reaction == 0u &&
                   n.candidate_layout == n.current_layout &&
                   n.dwell_ticks >= DIR_STAGNATION_DWELL_TICKS &&
                   n.novelty_accum_q31_ticks >= DIR_STAGNATION_THRESHOLD) {
            desired = dir_alternate(n.current_layout, n.seed, n.layout_epoch);
            n.previous_layout = n.current_layout; n.current_layout = desired;
            n.candidate_layout = desired; n.candidate_ticks = 1u;
            n.transition_ticks_left = DIR_TRANSITION_TICKS; n.transition_total_ticks = DIR_TRANSITION_TICKS;
            n.dwell_ticks = 0u; n.novelty_accum_q31_ticks = 0u; ++n.layout_epoch; shifted = 1u;
        }
    }

    phase_step = UINT32_C(1048576) + (n.macro_energy_q31 >> 7u) + (novelty >> 9u);
    if (strict_reaction != 0u) n.phase = 0u;
    else n.phase += phase_step;
    n.next_tick_index = c->tick_index + 1u;

    if (n.transition_ticks_left == 0u) {
        transition = COMP_Q31_ONE;
        n.transition_total_ticks = 0u;
    } else {
        uint32_t total = n.transition_total_ticks;
        uint32_t elapsed;
        if (total == 0u || n.transition_ticks_left > total) return ODM_STATUS_INVARIANT_BROKEN;
        elapsed = total - n.transition_ticks_left;
        transition = (uint32_t)(((uint64_t)elapsed * (uint64_t)COMP_Q31_ONE + total / 2u) / total);
        --n.transition_ticks_left;
    }

    a = dir_targets(n.previous_layout);
    b = dir_targets(n.current_layout);
    memset(&f, 0, sizeof(f));
    f.schema_version = ODM_DIRECTOR_SCHEMA_VERSION;
    f.layout = n.current_layout; f.previous_layout = n.previous_layout;
    if (shifted != 0u) f.flags |= ODM_DIRECTOR_FLAG_LAYOUT_SHIFT;
    if (recovery != 0u) f.flags |= ODM_DIRECTOR_FLAG_RECOVERY;
    if (impact_flag != 0u) f.flags |= ODM_DIRECTOR_FLAG_IMPACT;
    f.tick_index = c->tick_index; f.transition_q31 = transition;
    f.macro_energy_q31 = n.macro_energy_q31; f.macro_novelty_q31 = novelty;

    f.core_extent_q31 = dir_blend(a.core, b.core, transition);
    /* High energy opens the environment by reducing the core no more than 4%. */
    {
        uint32_t shrink = comp_mul_u31(n.macro_energy_q31, DIR_Q31_004);
        f.core_extent_q31 = f.core_extent_q31 > shrink ? f.core_extent_q31 - shrink : DIR_Q31_040;
    }
    f.orbit_q31 = comp_mul_u31(dir_blend(a.orbit,b.orbit,transition),
                               comp_lerp_u31(COMP_Q31_025,COMP_Q31_ONE,comp_max_u32(n.macro_energy_q31,radial_activity)));
    f.wings_q31 = comp_mul_u31(dir_blend(a.wings,b.wings,transition),
                               comp_lerp_u31(COMP_Q31_020,COMP_Q31_ONE,n.macro_width_q31));
    f.memory_panels_q31 = comp_mul_u31(dir_blend(a.memory,b.memory,transition),
                                       comp_lerp_u31(COMP_Q31_015,COMP_Q31_ONE,comp_max_u32(n.macro_memory_q31,novelty/3u)));
    f.tunnel_q31 = comp_mul_u31(dir_blend(a.tunnel,b.tunnel,transition),
                                comp_lerp_u31(COMP_Q31_020,COMP_Q31_ONE,n.macro_energy_q31));
    f.architecture_q31 = comp_mul_u31(dir_blend(a.architecture,b.architecture,transition),
                                      comp_lerp_u31(COMP_Q31_035,COMP_Q31_ONE,n.macro_energy_q31));
    f.backdrop_q31 = comp_mul_u31(dir_blend(a.backdrop,b.backdrop,transition),
                                  comp_lerp_u31(COMP_Q31_020,COMP_Q31_ONE,comp_max_u32(n.macro_memory_q31,n.macro_width_q31/2u)));
    f.depth_q31 = comp_mul_u31(dir_blend(a.depth,b.depth,transition),
                               comp_lerp_u31(COMP_Q31_035,COMP_Q31_ONE,comp_max_u32(n.macro_width_q31,n.macro_energy_q31)));
    activity = comp_max_u32(n.macro_energy_q31, novelty);
    f.edge_activity_q31 = comp_mul_u31(dir_blend(a.edge,b.edge,transition),
                                       comp_lerp_u31(COMP_Q31_020,COMP_Q31_ONE,activity));
    asym_gain = comp_mul_u31(n.macro_width_q31, COMP_Q31_012);
    if (strict_reaction != 0u) {
        /* Strict Music-Reaction path: the Director may transform only in
         * response to typed Composition evidence. Seed/time must not invent
         * asymmetry, orbit, or stagnation layout changes. */
        f.asymmetry_x_q31 = 0;
        f.asymmetry_y_q31 = 0;
        f.orbit_phase = 0u;
    } else {
        wx = comp_triangle_q31(n.phase);
        wy = comp_triangle_q31(n.phase + UINT32_C(0x40000000));
        f.asymmetry_x_q31 = comp_scaled_signed(wx,asym_gain);
        f.asymmetry_y_q31 = comp_scaled_signed(wy,asym_gain/2u);
        f.orbit_phase = c->ring_phase + n.phase;
    }
    f.layout_epoch = n.layout_epoch;
    f.layer_count = b.layers; f.pane_count = b.panes;

    *state = n;
    *out_frame = f;
    return ODM_STATUS_OK;
}

odm_status odm_director_resolve_batch(odm_director_state *state,
                                      const odm_composition_frame_state *frames,
                                      uint64_t frame_count,
                                      odm_director_frame_state *out_frames,
                                      uint64_t frame_capacity) {
    odm_director_state tmp;
    uint64_t i, expected;
    odm_status st;
    if (!state) return ODM_STATUS_INVALID_ARGUMENT;
    if (state->schema_version != ODM_DIRECTOR_SCHEMA_VERSION) return ODM_STATUS_VERSION_MISMATCH;
    if (frame_count == 0u) return ODM_STATUS_OK;
    if (!frames || !out_frames) return ODM_STATUS_INVALID_ARGUMENT;
    if (frame_capacity < frame_count) return ODM_STATUS_BUFFER_TOO_SMALL;
    expected = state->initialized != 0u ? state->next_tick_index : frames[0].tick_index;
    for (i = 0u; i < frame_count; ++i) {
        if (frames[i].schema_version != ODM_COMPOSITION_SCHEMA_VERSION) return ODM_STATUS_VERSION_MISMATCH;
        if (frames[i].mode > ODM_COMPOSITION_MODE_IMPACT || frames[i].tick_index != expected)
            return ODM_STATUS_INVALID_DATA;
        if (expected == UINT64_MAX) return ODM_STATUS_OVERFLOW;
        ++expected;
    }
    tmp = *state;
    for (i = 0u; i < frame_count; ++i) {
        st = odm_director_resolve_tick(&tmp, &frames[i], &out_frames[i]);
        if (st != ODM_STATUS_OK) return ODM_STATUS_INVARIANT_BROKEN;
    }
    *state = tmp;
    return ODM_STATUS_OK;
}


/* ================= Visual Policy v1 =================
 *
 * Canonical semantic identity for Advanced Composition v1 + Visual Director
 * v3.  This is intentionally NOT a dump of C structs: every field is emitted
 * little-endian in a stable order and the remaining bytes are canonical zero.
 * Source identity still binds implementation bytes; this policy binds the
 * visual meaning that a persisted Composition/Director stream claims to use.
 */
#define VISUAL_POLICY_MAGIC "ODMVPOL1"
#define VISUAL_POLICY_PROFILE_ID UINT32_C(1)
#define VISUAL_POLICY_COMPOSITION_ID UINT32_C(2)
#define VISUAL_POLICY_DIRECTOR_ID UINT32_C(3)
#define VISUAL_POLICY_TRANSITION_ID UINT32_C(1)
#define VISUAL_POLICY_TARGET_TABLE_ID UINT32_C(1)
#define VISUAL_POLICY_PHASE_ID UINT32_C(1)

static odm_status visual_policy_encode(uint8_t *buffer, uint64_t capacity,
                                       uint64_t *out_required) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    uint64_t written = 0u;
    odm_status st;
    uint32_t layout;
    static const uint32_t alternates[5] = {
        ODM_DIRECTOR_LAYOUT_MONOLITH, ODM_DIRECTOR_LAYOUT_ORBIT,
        ODM_DIRECTOR_LAYOUT_WINGS, ODM_DIRECTOR_LAYOUT_MEMORY,
        ODM_DIRECTOR_LAYOUT_EXPAND
    };

    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = ODM_VISUAL_POLICY_BYTES;
    if (!buffer || capacity < ODM_VISUAL_POLICY_BYTES)
        return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, ODM_VISUAL_POLICY_BYTES);
    st = odm_wire_writer_init(&w, buffer, ODM_VISUAL_POLICY_BYTES);
    if (st != ODM_STATUS_OK) return st;
#define VP_WRITE(call) do { st = (call); if (st != ODM_STATUS_OK) return st; } while (0)
    VP_WRITE(odm_wire_write_bytes(&w, VISUAL_POLICY_MAGIC, 8u));
    VP_WRITE(odm_wire_write_u32(&w, ODM_VISUAL_POLICY_VERSION));
    VP_WRITE(odm_wire_write_u32(&w, ODM_COMPOSITION_SCHEMA_VERSION));
    VP_WRITE(odm_wire_write_u32(&w, ODM_DIRECTOR_SCHEMA_VERSION));
    VP_WRITE(odm_wire_write_u32(&w, ODM_MUSIC_TICK_RATE));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_ONE));
    VP_WRITE(odm_wire_write_u32(&w, ODM_MUSIC_BAND_COUNT));
    VP_WRITE(odm_wire_write_u32(&w, ODM_COMPOSITION_RADIAL_SEGMENTS)); /* legacy */
    VP_WRITE(odm_wire_write_u32(&w, ODM_COMPOSITION_RADIAL_SEGMENTS_MAX)); /* reaction hires */
    VP_WRITE(odm_wire_write_u32(&w, ODM_COMPOSITION_FLAG_RADIAL_HIRES));
    VP_WRITE(odm_wire_write_u32(&w, ODM_COMPOSITION_FLAG_STRICT_CAUSAL));
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* strict-causal autonomous phase forbidden */
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* strict-causal director seed cannot alter geometry */
    VP_WRITE(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_FRAME_CONTEXTUAL_SALIENCE));
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* contextual macro authority: salience, not raw event strength */
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* local 96-radial evidence remains unmodified by salience */
    VP_WRITE(odm_wire_write_u32(&w, ODM_MUSIC_REACTION_EVENT_PROJECTION_SCHEMA_VERSION));
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* sample presentation replaces coarse global event authority */
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* exact event strength basis is published once in effective-sample interval */
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* sample-domain macro pulse never alters local lane morphology */
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* subtick trace flag is not a visual event-class bit */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_020)); /* multi-axis accent gate */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_055)); /* multi-axis impact gate */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_080)); /* multi-axis structural gate */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_035)); /* macro-pulse memory gate */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_085)); /* glitch gate */
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* global controls use typed causal axes */
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* strict Director radial authority = mean visible radial evidence */
    VP_WRITE(odm_wire_write_u32(&w, ODM_COMPOSITION_RADIAL_SEGMENTS_MAX));
    VP_WRITE(odm_wire_write_u32(&w, 4u)); /* composition modes */
    VP_WRITE(odm_wire_write_u32(&w, 7u)); /* director layouts */
    VP_WRITE(odm_wire_write_u32(&w, VISUAL_POLICY_PROFILE_ID));
    VP_WRITE(odm_wire_write_u32(&w, VISUAL_POLICY_COMPOSITION_ID));
    VP_WRITE(odm_wire_write_u32(&w, VISUAL_POLICY_DIRECTOR_ID));
    VP_WRITE(odm_wire_write_u32(&w, VISUAL_POLICY_TRANSITION_ID));
    VP_WRITE(odm_wire_write_u32(&w, VISUAL_POLICY_TARGET_TABLE_ID));
    VP_WRITE(odm_wire_write_u32(&w, VISUAL_POLICY_PHASE_ID));

    /* Composition v1 / profiled resolver semantic constants. */
    VP_WRITE(odm_wire_write_u32(&w, 3u)); /* profiled transient cubic power */
    VP_WRITE(odm_wire_write_u32(&w, 3u)); /* energy smoothing shift */
    VP_WRITE(odm_wire_write_u32(&w, 4u)); /* width smoothing shift */
    VP_WRITE(odm_wire_write_u32(&w, 3u)); /* high smoothing shift */
    VP_WRITE(odm_wire_write_u32(&w, 18u)); /* impact decay divisor */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_018)); /* glitch threshold */
    VP_WRITE(odm_wire_write_u32(&w, 12u)); /* glitch cooldown ticks */
    VP_WRITE(odm_wire_write_u32(&w, UINT32_C(6291456))); /* phase base */
    VP_WRITE(odm_wire_write_u32(&w, 8u)); /* phase energy shift */
    VP_WRITE(odm_wire_write_u32(&w, 10u)); /* phase width shift */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_025)); /* IMPACT mode */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_010)); /* FLOW mode */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_015)); /* max content drift */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_048));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_058));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_020)); /* breath max */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_025));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_060));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_080)); /* halo low gain */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_025)); /* halo energy gain */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_018));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_090));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_045));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_090));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_010));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_080));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_005));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_030));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_090)); /* particle high */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_025)); /* particle impact */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_070)); /* memory */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_090)); /* fracture impact */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_035)); /* fracture normal */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_090)); /* chroma glitch */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_025)); /* chroma normal */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_005));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_020));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_055));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_070));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_018)); /* non-silence void base */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_080)); /* radial band gain */
    VP_WRITE(odm_wire_write_u32(&w, 5u)); /* radial impact phase divisor */
    VP_WRITE(odm_wire_write_u32(&w, UINT32_C(8421504))); /* phase byte -> Q31 */
    VP_WRITE(odm_wire_write_u32(&w, 2u)); /* vertical drift divisor */
    VP_WRITE(odm_wire_write_u32(&w, UINT32_C(2654435761))); /* ring phase stride */

    /* Director v3 normalization, thresholds, dwell and transition policy. */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_048));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_058));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_010));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_080));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_070)); /* memory normalization ref */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_090)); /* fracture normalization ref */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_065)); /* VOID candidate */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_045)); /* FRACTURE candidate */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_035)); /* MEMORY candidate */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_055)); /* WINGS candidate */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_065)); /* EXPAND candidate */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_055)); /* ORBIT radial */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_050)); /* ORBIT halo */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_012)); /* VOID recovery energy */
    VP_WRITE(odm_wire_write_u32(&w, DIR_Q31_040)); /* raw fracture gate */
    VP_WRITE(odm_wire_write_u32(&w, 5u));
    VP_WRITE(odm_wire_write_u32(&w, 5u));
    VP_WRITE(odm_wire_write_u32(&w, 4u));
    VP_WRITE(odm_wire_write_u32(&w, 3u));
    VP_WRITE(odm_wire_write_u32(&w, DIR_MIN_DWELL_TICKS));
    VP_WRITE(odm_wire_write_u32(&w, DIR_CANDIDATE_STABLE_TICKS));
    VP_WRITE(odm_wire_write_u32(&w, DIR_FRACTURE_STABLE_TICKS));
    VP_WRITE(odm_wire_write_u32(&w, DIR_FRACTURE_DWELL_TICKS));
    VP_WRITE(odm_wire_write_u32(&w, DIR_FRACTURE_RELEASE_STABLE_TICKS));
    VP_WRITE(odm_wire_write_u32(&w, DIR_FRACTURE_MIN_HOLD_TICKS));
    VP_WRITE(odm_wire_write_u32(&w, DIR_VOID_STABLE_TICKS));
    VP_WRITE(odm_wire_write_u32(&w, DIR_RECOVERY_STABLE_TICKS));
    VP_WRITE(odm_wire_write_u32(&w, DIR_STAGNATION_DWELL_TICKS));
    VP_WRITE(odm_wire_write_u32(&w, DIR_TRANSITION_TICKS));
    VP_WRITE(odm_wire_write_u32(&w, DIR_FAST_TRANSITION_TICKS));
    VP_WRITE(odm_wire_write_u64(&w, DIR_NOVELTY_THRESHOLD));
    VP_WRITE(odm_wire_write_u64(&w, DIR_STAGNATION_THRESHOLD));
    VP_WRITE(odm_wire_write_u32(&w, UINT32_C(1048576))); /* director phase base */
    VP_WRITE(odm_wire_write_u32(&w, 7u)); /* phase energy shift */
    VP_WRITE(odm_wire_write_u32(&w, 9u)); /* phase novelty shift */
    VP_WRITE(odm_wire_write_u32(&w, DIR_Q31_004)); /* max core shrink */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_025));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_020));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_015));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_020));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_035));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_020));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_035));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_020));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_012)); /* asymmetry gain */
    VP_WRITE(odm_wire_write_u32(&w, UINT32_C(0xa5a5a5a5))); /* phase seed xor */
    VP_WRITE(odm_wire_write_u64(&w, UINT64_C(0x9e3779b97f4a7c15)));
    VP_WRITE(odm_wire_write_u64(&w, UINT64_C(0xbf58476d1ce4e5b9)));
    VP_WRITE(odm_wire_write_u64(&w, UINT64_C(0x94d049bb133111eb)));
    for (layout = 0u; layout < 5u; ++layout)
        VP_WRITE(odm_wire_write_u32(&w, alternates[layout]));

    /* Complete macro-layout target table, including counts. */
    for (layout = 0u; layout < 7u; ++layout) {
        odm_dir_targets t = dir_targets(layout);
        VP_WRITE(odm_wire_write_u32(&w, layout));
        VP_WRITE(odm_wire_write_u32(&w, t.core));
        VP_WRITE(odm_wire_write_u32(&w, t.orbit));
        VP_WRITE(odm_wire_write_u32(&w, t.wings));
        VP_WRITE(odm_wire_write_u32(&w, t.memory));
        VP_WRITE(odm_wire_write_u32(&w, t.tunnel));
        VP_WRITE(odm_wire_write_u32(&w, t.architecture));
        VP_WRITE(odm_wire_write_u32(&w, t.backdrop));
        VP_WRITE(odm_wire_write_u32(&w, t.depth));
        VP_WRITE(odm_wire_write_u32(&w, t.edge));
        VP_WRITE(odm_wire_write_u32(&w, t.layers));
        VP_WRITE(odm_wire_write_u32(&w, t.panes));
    }
    /* Multiscale causal radial morphology/provenance contract. */
    VP_WRITE(odm_wire_write_u32(&w, 3u)); /* morphology id: fast body -> release -> attack */
    VP_WRITE(odm_wire_write_u32(&w, ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE));
    VP_WRITE(odm_wire_write_u32(&w, ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE));
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_045)); /* fast-envelope body ceiling */
    VP_WRITE(odm_wire_write_u32(&w, COMP_Q31_035)); /* slow-fast release weight */
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* same-lane body/release/attack only */
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* release = exact slow-fast excess */
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* attack consumes post-release headroom */
    VP_WRITE(odm_wire_write_u32(&w, 1u)); /* full attack reaches exact full range */
    VP_WRITE(odm_wire_writer_finish(&w, &written));
#undef VP_WRITE
    if (written > ODM_VISUAL_POLICY_BYTES) return ODM_STATUS_INVARIANT_BROKEN;
    return ODM_STATUS_OK;
}

odm_status odm_visual_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                   uint64_t *out_required) {
    return visual_policy_encode(buffer, capacity, out_required);
}

odm_status odm_visual_policy_current_sha256(odm_sha256_digest *out_hash) {
    uint8_t bytes[ODM_VISUAL_POLICY_BYTES];
    uint64_t required = 0u;
    odm_status st;
    if (!out_hash) return ODM_STATUS_INVALID_ARGUMENT;
    st = visual_policy_encode(bytes, sizeof(bytes), &required);
    if (st != ODM_STATUS_OK) return st;
    if (required != sizeof(bytes)) return ODM_STATUS_INVARIANT_BROKEN;
    return odm_sha256(bytes, sizeof(bytes), out_hash);
}
