#include "odm_media.h"
#include "odm_music_map.h"
#include "odm_music_reaction.h"
#include "odm_visual.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    uint64_t tick;
    uint32_t strength;
    uint32_t flags;
} event_row;

typedef struct {
    uint64_t ticks;
    uint64_t changing_ticks;
    uint64_t active_radial_total;
    uint64_t distinct_bucket_total;
    uint64_t radial_l1_total;
    uint64_t repeat6_l1_total;
} radial_metrics;

static double now_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int read_all(const char *path, uint8_t **out, uint64_t *out_n) {
    FILE *f; long n; uint8_t *p; size_t got;
    if (!path || !out || !out_n) return 0;
    *out = NULL; *out_n = 0u;
    f = fopen(path, "rb"); if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    n = ftell(f); if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    p = (uint8_t *)malloc((size_t)n); if (!p) { fclose(f); return 0; }
    got = fread(p, 1u, (size_t)n, f); fclose(f);
    if (got != (size_t)n) { free(p); return 0; }
    *out = p; *out_n = (uint64_t)n; return 1;
}

static uint64_t uabsdiff(uint32_t a, uint32_t b) {
    return a >= b ? (uint64_t)(a - b) : (uint64_t)(b - a);
}

static void radial_accumulate(radial_metrics *m, const uint32_t *r, const uint32_t *prev, uint32_t count, int have_prev) {
    uint8_t seen[256];
    uint32_t i;
    uint32_t active = 0u;
    uint32_t distinct = 0u;
    uint64_t temporal = 0u;
    uint64_t repeat6 = 0u;
    const uint32_t threshold = (uint32_t)((uint64_t)INT32_MAX * 15u / 100u);
    memset(seen, 0, sizeof(seen));
    for (i = 0u; i < count; ++i) {
        uint32_t bucket = r[i] >> 23;
        if (bucket > 255u) bucket = 255u;
        if (seen[bucket] == 0u) { seen[bucket] = 1u; ++distinct; }
        if (r[i] >= threshold) ++active;
        if (have_prev) temporal += uabsdiff(r[i], prev[i]);
        if (i + 6u < count) repeat6 += uabsdiff(r[i], r[i + 6u]);
    }
    ++m->ticks;
    m->active_radial_total += active;
    m->distinct_bucket_total += distinct;
    m->radial_l1_total += temporal;
    m->repeat6_l1_total += repeat6;
    if (have_prev && temporal != 0u) ++m->changing_ticks;
}

static void print_q31_decimal(uint64_t sum, uint64_t count, uint64_t scale) {
    double v = count == 0u ? 0.0 : (double)sum / (double)count / (double)scale;
    printf("%.6f", v);
}

int main(int argc, char **argv) {
    uint8_t *wav = NULL;
    uint64_t wav_n = 0u, pcm_n = 0u, ticks_n = 0u;
    odm_pcm_stereo_q31 *pcm = NULL;
    odm_media_facts facts;
    int32_t *window = NULL;
    odm_music_complex_q31 *twiddles = NULL;
    odm_music_analysis_plan plan;
    odm_music_analysis_state ast;
    odm_music_analysis_scratch scratch;
    odm_music_analysis_tick *ticks = NULL;
    odm_music_reaction_tick *rticks = NULL;
    odm_music_reaction_frame *rframes = NULL;
    odm_music_reaction_frame *refined = NULL;
    odm_music_reaction_event *events = NULL;
    odm_music_reaction_profile rprofile;
    odm_music_reaction_state rstate;
    odm_composition_profile legacy_profile;
    odm_composition_resolver_state legacy_state;
    odm_composition_frame_state cnew, cold;
    uint32_t prev_new96[ODM_COMPOSITION_RADIAL_SEGMENTS_MAX];
    uint32_t prev_new48[ODM_COMPOSITION_RADIAL_SEGMENTS];
    uint32_t prev_old48[ODM_COMPOSITION_RADIAL_SEGMENTS];
    radial_metrics mn96, mn48, mo48;
    event_row top[32];
    uint32_t top_n = 0u;
    uint64_t streaming_events = 0u, refined_events = 0u;
    uint64_t provenance_events = 0u;
    uint32_t provenance_macro_gate = 0u;
    uint64_t timing_abs_samples_sum = 0u;
    uint32_t timing_abs_samples_max = 0u;
    uint64_t timing_confidence_sum = 0u;
    uint32_t timing_confidence_min = (uint32_t)INT32_MAX;
    uint32_t timing_confidence_max = 0u;
    uint64_t family_events[ODM_MUSIC_REACTION_FAMILY_COUNT] = {0};
    uint64_t event_strength_sum = 0u;
    uint64_t event_strength_bins[10] = {0};
    uint64_t macro_salience_sum = 0u;
    uint64_t macro_salience_bins[10] = {0};
    uint32_t event_strength_min = (uint32_t)INT32_MAX;
    uint32_t event_strength_max = 0u;
    uint32_t macro_salience_min = (uint32_t)INT32_MAX;
    uint32_t macro_salience_max = 0u;
    uint64_t attack_nonzero = 0u;
    uint64_t i;
    double t0, t1, t2, t3, t4, t5;
    odm_status st;

    if (argc != 2) { fprintf(stderr, "usage: %s <wav>\n", argv[0]); return 2; }
    memset(&facts, 0, sizeof(facts)); memset(&plan, 0, sizeof(plan));
    memset(&ast, 0, sizeof(ast)); memset(&scratch, 0, sizeof(scratch));
    memset(&rprofile, 0, sizeof(rprofile)); memset(&rstate, 0, sizeof(rstate));
    memset(&legacy_profile, 0, sizeof(legacy_profile)); memset(&legacy_state, 0, sizeof(legacy_state));
    memset(&mn96, 0, sizeof(mn96)); memset(&mn48, 0, sizeof(mn48)); memset(&mo48, 0, sizeof(mo48)); memset(top, 0, sizeof(top));
    memset(prev_new96, 0, sizeof(prev_new96)); memset(prev_new48, 0, sizeof(prev_new48)); memset(prev_old48, 0, sizeof(prev_old48));

    if (!read_all(argv[1], &wav, &wav_n)) { fprintf(stderr, "read failed\n"); return 3; }
    st = odm_media_decode_audio_q31(wav, wav_n, NULL, 0u, &pcm_n, &facts);
    if (st != ODM_STATUS_BUFFER_TOO_SMALL || pcm_n == 0u) { fprintf(stderr, "wav sizing failed %d\n", (int)st); return 4; }
    pcm = (odm_pcm_stereo_q31 *)malloc((size_t)pcm_n * sizeof(*pcm));
    if (!pcm) return 5;
    st = odm_media_decode_audio_q31(wav, wav_n, pcm, pcm_n, &pcm_n, &facts);
    free(wav); wav = NULL;
    if (st != ODM_STATUS_OK) { fprintf(stderr, "wav decode failed %d\n", (int)st); return 6; }
    if (facts.sample_rate != ODM_MUSIC_SAMPLE_RATE) { fprintf(stderr, "probe requires canonical 48k input\n"); return 7; }

    window = (int32_t *)malloc((size_t)ODM_MUSIC_WINDOW_SAMPLES * sizeof(*window));
    twiddles = (odm_music_complex_q31 *)malloc((size_t)ODM_MUSIC_FFT_TWIDDLES * sizeof(*twiddles));
    if (!window || !twiddles) return 8;
    if (odm_music_analysis_plan_build(window, ODM_MUSIC_WINDOW_SAMPLES, twiddles, ODM_MUSIC_FFT_TWIDDLES, &plan) != ODM_STATUS_OK) return 9;
    if (odm_music_tick_count(pcm_n, &ticks_n) != ODM_STATUS_OK || ticks_n == 0u) return 10;
    ticks = (odm_music_analysis_tick *)calloc((size_t)ticks_n, sizeof(*ticks));
    rticks = (odm_music_reaction_tick *)calloc((size_t)ticks_n, sizeof(*rticks));
    rframes = (odm_music_reaction_frame *)calloc((size_t)ticks_n, sizeof(*rframes));
    refined = (odm_music_reaction_frame *)calloc((size_t)ticks_n, sizeof(*refined));
    if (!ticks || !rticks || !rframes || !refined) return 11;
    if (odm_music_analysis_state_init(&ast) != ODM_STATUS_OK) return 12;

    t0 = now_seconds();
    for (i = 0u; i < ticks_n; ++i) {
        st = odm_music_analyze_tick(&plan, window, twiddles, pcm, pcm_n, i, &ast, &scratch, &ticks[i]);
        if (st != ODM_STATUS_OK) { fprintf(stderr, "analysis failed tick=%" PRIu64 " st=%d\n", i, (int)st); return 13; }
        st = odm_music_reaction_extract_tick(&ticks[i], &scratch, &rticks[i]);
        if (st != ODM_STATUS_OK) { fprintf(stderr, "reaction extract failed tick=%" PRIu64 " st=%d\n", i, (int)st); return 14; }
    }
    t1 = now_seconds();
    if (odm_music_reaction_profile_build(rticks, ticks_n, &rprofile) != ODM_STATUS_OK) return 15;
    t2 = now_seconds();
    if (odm_composition_profile_build(ticks, ticks_n, &legacy_profile) != ODM_STATUS_OK) return 16;
    if (odm_music_reaction_state_init(&rstate) != ODM_STATUS_OK) return 17;
    if (odm_composition_resolver_init(&legacy_state, UINT64_C(0x1500cafe12345678)) != ODM_STATUS_OK) return 18;

    for (i = 0u; i < ticks_n; ++i) {
        st = odm_music_reaction_resolve_tick(&rstate, &rprofile, &rticks[i], &rframes[i]);
        if (st != ODM_STATUS_OK) { fprintf(stderr, "reaction resolve failed tick=%" PRIu64 " st=%d\n",i,(int)st); return 19; }
        if ((rframes[i].event_flags & ODM_MUSIC_REACTION_EVENT_ANY) != 0u) ++streaming_events;
    }
    t3 = now_seconds();
    st = odm_music_reaction_refine_events_offline(rframes, ticks_n, refined, ticks_n);
    if (st != ODM_STATUS_OK) { fprintf(stderr, "offline refinement failed st=%d\n", (int)st); return 20; }
    t4 = now_seconds();
    st = odm_music_reaction_build_events_offline(rframes, ticks_n, NULL, 0u,
                                                  &provenance_events, &provenance_macro_gate);
    if (st != ODM_STATUS_OK) { fprintf(stderr, "event query failed st=%d\n", (int)st); return 23; }
    if (provenance_events != 0u) {
        events = (odm_music_reaction_event *)calloc((size_t)provenance_events, sizeof(*events));
        if (!events) return 24;
        st = odm_music_reaction_build_events_offline(rframes, ticks_n, events, provenance_events,
                                                      &provenance_events, &provenance_macro_gate);
        if (st != ODM_STATUS_OK) { fprintf(stderr, "event build failed st=%d\n", (int)st); return 25; }
    }
    t5 = now_seconds();
    for (i = 0u; i < provenance_events; ++i) {
        uint32_t abs_off = events[i].refined_offset_samples < 0 ?
            (uint32_t)(-(int64_t)events[i].refined_offset_samples) :
            (uint32_t)events[i].refined_offset_samples;
        timing_abs_samples_sum += abs_off;
        if (abs_off > timing_abs_samples_max) timing_abs_samples_max = abs_off;
        timing_confidence_sum += events[i].timing_confidence_q31;
        if (events[i].timing_confidence_q31 < timing_confidence_min)
            timing_confidence_min = events[i].timing_confidence_q31;
        if (events[i].timing_confidence_q31 > timing_confidence_max)
            timing_confidence_max = events[i].timing_confidence_q31;
    }

    for (i = 0u; i < ticks_n; ++i) {
        const odm_music_reaction_frame *rf = &refined[i];
        uint32_t f;
        uint32_t reduced_new48[ODM_COMPOSITION_RADIAL_SEGMENTS];
        st = odm_composition_resolve_music_reaction(&ticks[i], rf, &cnew);
        if (st != ODM_STATUS_OK) return 21;
        st = odm_composition_resolve_tick_profiled(&legacy_state, &legacy_profile, &ticks[i], &cold);
        if (st != ODM_STATUS_OK) return 22;
        for (f = 0u; f < ODM_COMPOSITION_RADIAL_SEGMENTS; ++f) {
            uint32_t a = cnew.radial_q31[f * 2u];
            uint32_t b = cnew.radial_q31[f * 2u + 1u];
            reduced_new48[f] = a > b ? a : b;
        }
        radial_accumulate(&mn96, cnew.radial_q31, prev_new96, ODM_COMPOSITION_RADIAL_SEGMENTS_MAX, i != 0u);
        radial_accumulate(&mn48, reduced_new48, prev_new48, ODM_COMPOSITION_RADIAL_SEGMENTS, i != 0u);
        radial_accumulate(&mo48, cold.radial_q31, prev_old48, ODM_COMPOSITION_RADIAL_SEGMENTS, i != 0u);
        memcpy(prev_new96, cnew.radial_q31, sizeof(prev_new96));
        memcpy(prev_new48, reduced_new48, sizeof(prev_new48));
        memcpy(prev_old48, cold.radial_q31, sizeof(prev_old48));
        if (rf->broadband_attack_q31 != 0u) ++attack_nonzero;
        if ((rf->event_flags & ODM_MUSIC_REACTION_EVENT_ANY) != 0u) {
            uint32_t pos = top_n;
            uint32_t bin;
            ++refined_events; event_strength_sum += rf->event_strength_q31;
            if (rf->event_strength_q31 < event_strength_min) event_strength_min = rf->event_strength_q31;
            if (rf->event_strength_q31 > event_strength_max) event_strength_max = rf->event_strength_q31;
            bin = (uint32_t)(((uint64_t)rf->event_strength_q31 * UINT64_C(10)) / ((uint64_t)INT32_MAX + UINT64_C(1)));
            if (bin > 9u) bin = 9u;
            ++event_strength_bins[bin];
            macro_salience_sum += rf->macro_salience_q31;
            if (rf->macro_salience_q31 < macro_salience_min) macro_salience_min = rf->macro_salience_q31;
            if (rf->macro_salience_q31 > macro_salience_max) macro_salience_max = rf->macro_salience_q31;
            bin = (uint32_t)(((uint64_t)rf->macro_salience_q31 * UINT64_C(10)) / ((uint64_t)INT32_MAX + UINT64_C(1)));
            if (bin > 9u) bin = 9u;
            ++macro_salience_bins[bin];
            for (f = 0u; f < ODM_MUSIC_REACTION_FAMILY_COUNT; ++f)
                if ((rf->event_flags & (UINT32_C(1) << f)) != 0u) ++family_events[f];
            if (pos < 32u) ++top_n; else { pos = 31u; if (rf->event_strength_q31 <= top[pos].strength) continue; }
            top[pos].tick = i; top[pos].strength = rf->event_strength_q31; top[pos].flags = rf->event_flags;
            while (pos > 0u && top[pos].strength > top[pos-1u].strength) {
                event_row tmp = top[pos-1u]; top[pos-1u] = top[pos]; top[pos] = tmp; --pos;
            }
        }
    }
    printf("{\n");
    printf("  \"pcm_frames\": %" PRIu64 ",\n", pcm_n);
    printf("  \"ticks\": %" PRIu64 ",\n", ticks_n);
    printf("  \"duration_seconds\": %.6f,\n", (double)pcm_n / 48000.0);
    printf("  \"analysis_extract_seconds\": %.6f,\n", t1-t0);
    printf("  \"profile_seconds\": %.6f,\n", t2-t1);
    printf("  \"streaming_resolve_seconds\": %.6f,\n", t3-t2);
    printf("  \"offline_refine_seconds\": %.6f,\n", t4-t3);
    printf("  \"event_provenance_build_seconds\": %.6f,\n", t5-t4);
    printf("  \"event_provenance_records\": %" PRIu64 ",\n", provenance_events);
    printf("  \"event_provenance_macro_gate\": %.6f,\n", (double)provenance_macro_gate/(double)INT32_MAX);
    printf("  \"mean_refined_timing_abs_ms\": %.6f,\n", provenance_events ? ((double)timing_abs_samples_sum/(double)provenance_events)/48.0 : 0.0);
    printf("  \"max_refined_timing_abs_ms\": %.6f,\n", (double)timing_abs_samples_max/48.0);
    printf("  \"mean_timing_confidence\": %.6f,\n", provenance_events ? (double)timing_confidence_sum/(double)provenance_events/(double)INT32_MAX : 0.0);
    printf("  \"min_timing_confidence\": %.6f,\n", provenance_events ? (double)timing_confidence_min/(double)INT32_MAX : 0.0);
    printf("  \"max_timing_confidence\": %.6f,\n", provenance_events ? (double)timing_confidence_max/(double)INT32_MAX : 0.0);
    printf("  \"streaming_reaction_events\": %" PRIu64 ",\n", streaming_events);
    printf("  \"offline_refined_events\": %" PRIu64 ",\n", refined_events);
    printf("  \"streaming_events_per_second\": %.6f,\n", (double)streaming_events / ((double)pcm_n/48000.0));
    printf("  \"offline_refined_events_per_second\": %.6f,\n", (double)refined_events / ((double)pcm_n/48000.0));
    printf("  \"nonzero_attack_tick_ratio\": %.6f,\n", (double)attack_nonzero/(double)ticks_n);
    printf("  \"mean_refined_event_strength\": "); print_q31_decimal(event_strength_sum, refined_events, (uint64_t)INT32_MAX); printf(",\n");
    printf("  \"min_refined_event_strength\": %.6f,\n", refined_events ? (double)event_strength_min/(double)INT32_MAX : 0.0);
    printf("  \"max_refined_event_strength\": %.6f,\n", refined_events ? (double)event_strength_max/(double)INT32_MAX : 0.0);
    printf("  \"event_strength_decile_bins\": ["); for(i=0;i<10u;i++){if(i)printf(", ");printf("%" PRIu64,event_strength_bins[i]);} printf("],\n");
    printf("  \"mean_macro_salience\": "); print_q31_decimal(macro_salience_sum, refined_events, (uint64_t)INT32_MAX); printf(",\n");
    printf("  \"min_macro_salience\": %.6f,\n", refined_events ? (double)macro_salience_min/(double)INT32_MAX : 0.0);
    printf("  \"max_macro_salience\": %.6f,\n", refined_events ? (double)macro_salience_max/(double)INT32_MAX : 0.0);
    printf("  \"macro_salience_decile_bins\": ["); for(i=0;i<10u;i++){if(i)printf(", ");printf("%" PRIu64,macro_salience_bins[i]);} printf("],\n");
    printf("  \"family_event_counts\": ["); for(i=0;i<6u;i++){if(i)printf(", ");printf("%" PRIu64,family_events[i]);} printf("],\n");
    printf("  \"new96_radial_mean_active_gt_0_15\": %.6f,\n", (double)mn96.active_radial_total/(double)mn96.ticks);
    printf("  \"new96_radial_active_fraction_gt_0_15\": %.6f,\n", (double)mn96.active_radial_total/((double)mn96.ticks*96.0));
    printf("  \"new48_reduced_mean_active_gt_0_15\": %.6f,\n", (double)mn48.active_radial_total/(double)mn48.ticks);
    printf("  \"legacy48_mean_active_gt_0_15\": %.6f,\n", (double)mo48.active_radial_total/(double)mo48.ticks);
    printf("  \"new96_radial_mean_quantized_distinct\": %.6f,\n", (double)mn96.distinct_bucket_total/(double)mn96.ticks);
    printf("  \"new96_radial_distinct_fraction\": %.6f,\n", (double)mn96.distinct_bucket_total/((double)mn96.ticks*96.0));
    printf("  \"new48_reduced_mean_quantized_distinct\": %.6f,\n", (double)mn48.distinct_bucket_total/(double)mn48.ticks);
    printf("  \"legacy48_mean_quantized_distinct\": %.6f,\n", (double)mo48.distinct_bucket_total/(double)mo48.ticks);
    printf("  \"new48_repeat6_mean_abs_q31\": "); print_q31_decimal(mn48.repeat6_l1_total, mn48.ticks*42u, 1u); printf(",\n");
    printf("  \"legacy48_repeat6_mean_abs_q31\": "); print_q31_decimal(mo48.repeat6_l1_total, mo48.ticks*42u, 1u); printf(",\n");
    printf("  \"new96_radial_changing_tick_ratio\": %.6f,\n", mn96.ticks>1u?(double)mn96.changing_ticks/(double)(mn96.ticks-1u):0.0);
    printf("  \"new48_reduced_changing_tick_ratio\": %.6f,\n", mn48.ticks>1u?(double)mn48.changing_ticks/(double)(mn48.ticks-1u):0.0);
    printf("  \"legacy48_changing_tick_ratio\": %.6f,\n", mo48.ticks>1u?(double)mo48.changing_ticks/(double)(mo48.ticks-1u):0.0);
    printf("  \"top_events\": [\n");
    for(i=0;i<top_n;i++) {
        printf("    {\"tick\": %" PRIu64 ", \"seconds\": %.3f, \"strength\": %.6f, \"flags\": %u}%s\n",
               top[i].tick, (double)top[i].tick/100.0, (double)top[i].strength/(double)INT32_MAX,
               top[i].flags, i+1u<top_n?",":"");
    }
    printf("  ]\n}\n");

    free(events); free(refined); free(rframes); free(rticks); free(ticks); free(twiddles); free(window); free(pcm);
    return 0;
}
