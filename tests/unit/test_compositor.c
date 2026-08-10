#include "test_harness.h"

#include "odm_compositor.h"
#include "odm_job.h"
#include "compositor_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t q31_ratio(uint32_t num, uint32_t den) {
    uint64_t v = (uint64_t)INT32_MAX * num + den / 2u;
    return (uint32_t)(v / den);
}

static void compositor_config(odm_layered_config *c, uint32_t w, uint32_t h,
                              uint32_t aspect) {
    memset(c, 0, sizeof(*c));
    c->schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c->canvas.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c->canvas.aspect = aspect;
    c->canvas.width = w;
    c->canvas.height = h;
    c->canvas.fps = 30;
    c->canvas.safe_left_q31 = q31_ratio(1u, 20u);
    c->canvas.safe_top_q31 = q31_ratio(1u, 20u);
    c->canvas.safe_right_q31 = q31_ratio(1u, 20u);
    c->canvas.safe_bottom_q31 = q31_ratio(1u, 20u);

    c->background.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c->background.style = ODM_BACKGROUND_GRID;
    c->background.solid_color.a = UINT16_MAX;
    c->background.grid_color.r = UINT16_MAX;
    c->background.grid_color.g = UINT16_MAX;
    c->background.grid_color.b = UINT16_MAX;
    c->background.grid_color.a = UINT16_MAX;
    c->background.grid_spacing_q16 = 8u << 16;
    c->background.grid_line_q16 = 1u << 16;
    c->background.grid_feather_q16 = 1u << 16;
    c->background.zoom_reactivity_q31 = q31_ratio(1u, 4u);
    c->background.warp_reactivity_q31 = q31_ratio(1u, 8u);
    c->background.depth_reactivity_q31 = q31_ratio(1u, 8u);
    c->background.opacity_q31 = (uint32_t)INT32_MAX;

    c->core.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c->core.shape = ODM_CORE_SHAPE_CIRCLE;
    c->core.fit = ODM_CORE_FIT_COVER;
    c->core.center_x_q31 = q31_ratio(1u, 2u);
    c->core.center_y_q31 = q31_ratio(1u, 2u);
    c->core.width_q31 = q31_ratio(1u, 2u);
    c->core.height_q31 = q31_ratio(1u, 2u);
    c->core.corner_radius_q31 = q31_ratio(1u, 8u);
    c->core.border_q16 = 1u << 16;
    c->core.feather_q16 = 1u << 16;
    c->core.scale_reactivity_q31 = q31_ratio(1u, 16u);
    c->core.opacity_q31 = (uint32_t)INT32_MAX;
    c->core.border_color.r = UINT16_MAX;
    c->core.border_color.g = UINT16_MAX;
    c->core.border_color.b = UINT16_MAX;
    c->core.border_color.a = UINT16_MAX;

    c->field.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c->field.flags = ODM_FIELD_RADIAL_BARS | ODM_FIELD_PARTICLES | ODM_FIELD_ORBIT_RING;
    c->field.radial_segments = ODM_COMPOSITION_RADIAL_SEGMENTS;
    c->field.particle_count = 8u;
    c->field.ring_gap_q16 = 2u << 16;
    c->field.bar_min_q16 = 1u << 16;
    c->field.bar_max_q16 = 4u << 16;
    c->field.bar_width_q16 = 1u << 16;
    c->field.particle_radius_q16 = 1u << 16;
    c->field.field_opacity_q31 = q31_ratio(3u, 4u);
    c->field.primary_color.r = UINT16_MAX;
    c->field.primary_color.g = UINT16_MAX;
    c->field.primary_color.b = UINT16_MAX;
    c->field.primary_color.a = UINT16_MAX;
    c->field.secondary_color.r = UINT16_MAX / 2u;
    c->field.secondary_color.g = UINT16_MAX / 2u;
    c->field.secondary_color.b = UINT16_MAX / 2u;
    c->field.secondary_color.a = UINT16_MAX;
    c->field.seed = UINT64_C(0x0d130d130d130d13);

    c->hud.schema_version = ODM_LAYERED_SCHEMA_VERSION;
    c->hud.flags = ODM_HUD_PROGRESS_BAR | ODM_HUD_TIME_CODE | ODM_HUD_TITLE | ODM_HUD_ARTIST;
    c->hud.margin_q16 = 2u << 16;
    c->hud.progress_height_q16 = 1u << 16;
    c->hud.progress_width_q31 = q31_ratio(3u, 4u);
    c->hud.text_scale_q16 = 1u << 16;
    c->hud.line_gap_q16 = 1u << 16;
    c->hud.opacity_q31 = (uint32_t)INT32_MAX;
    c->hud.foreground_color.r = UINT16_MAX;
    c->hud.foreground_color.g = UINT16_MAX;
    c->hud.foreground_color.b = UINT16_MAX;
    c->hud.foreground_color.a = UINT16_MAX;
    c->hud.background_color.a = UINT16_MAX;
    memcpy(c->hud.title, "QUIET, NOT EMPTY", 17u);
    memcpy(c->hud.artist, "AFTERIMAGE", 11u);
}

static void compositor_states(odm_composition_frame_state *comp,
                              odm_director_frame_state *dir) {
    uint32_t i;
    memset(comp, 0, sizeof(*comp));
    memset(dir, 0, sizeof(*dir));
    comp->schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
    comp->mode = ODM_COMPOSITION_MODE_FLOW;
    comp->tick_index = 50u;
    comp->center_sample = UINT64_C(24000);
    comp->phase = UINT32_C(0x12345678);
    comp->ring_phase = UINT32_C(0x34567890);
    comp->core_breath_q31 = q31_ratio(1u, 3u);
    comp->grid_q31 = q31_ratio(1u, 2u);
    comp->particles_q31 = q31_ratio(1u, 2u);
    comp->radial_gain_q31 = q31_ratio(3u, 4u);
    comp->fracture_q31 = q31_ratio(1u, 5u);
    for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS; ++i) {
        comp->radial_q31[i] = (uint32_t)(((uint64_t)INT32_MAX * (i + 1u)) /
                                         ODM_COMPOSITION_RADIAL_SEGMENTS);
    }
    dir->schema_version = ODM_DIRECTOR_SCHEMA_VERSION;
    dir->tick_index = comp->tick_index;
    dir->layout = ODM_DIRECTOR_LAYOUT_ORBIT;
    dir->depth_q31 = q31_ratio(2u, 5u);
    dir->edge_activity_q31 = q31_ratio(1u, 4u);
    dir->orbit_phase = UINT32_C(0x10203040);
}

static void fill_surface(uint8_t pixels[8u * 8u * 4u], odm_render_surface_frame *s) {
    uint32_t x, y;
    for (y = 0u; y < 8u; ++y) {
        for (x = 0u; x < 8u; ++x) {
            uint64_t o = ((uint64_t)y * 8u + x) * 4u;
            pixels[o + 0u] = (uint8_t)(32u + x * 24u);
            pixels[o + 1u] = (uint8_t)(16u + y * 26u);
            pixels[o + 2u] = (uint8_t)(255u - x * 12u - y * 7u);
            pixels[o + 3u] = UINT8_MAX;
        }
    }
    memset(s, 0, sizeof(*s));
    s->width = 8u; s->height = 8u;
    s->pixel_format = ODM_RENDER_SURFACE_RGBA8;
    s->primaries = ODM_COLOR_PRIMARIES_BT709;
    s->transfer = ODM_COLOR_TRANSFER_SRGB;
    s->alpha_mode = ODM_ALPHA_STRAIGHT;
    s->pixel_bytes = 8u * 8u * 4u;
    s->pixels = pixels;
}

/* LA COMPOSICION NO PUEDE CAMBIAR LA CANCION.
 *
 * Es la propiedad que costo conseguir y la que hay que defender: el usuario
 * elige otra composicion y el video cambia, pero la lectura de la musica no.
 * Se comprueba donde de verdad se decide -- el Frame Plan -- y no mirando
 * pixeles: el plan es el ultimo sitio donde la musica todavia es un valor y no
 * una forma. Si dos composiciones dan planes distintos, alguna de las dos esta
 * reinterpretando la evidencia.
 *
 * El hash de configuracion SI cambia, y debe cambiar: son configuraciones
 * distintas. Lo que no puede cambiar es lo que la musica vale. */
static void test_composicion_no_toca_la_musica(odm_test_context *context) {
    odm_layered_config base;
    odm_composition_frame_state comp;
    odm_director_frame_state dir;
    odm_layered_frame_plan primero;
    uint32_t layout, i;
    int primero_listo = 0;

    if (odm_layered_config_init_default(&base, ODM_CANVAS_ASPECT_HORIZONTAL_16_9,
                                        1280u, 720u, 30) != ODM_STATUS_OK) {
        ODM_TEST_CHECK(context, 0);
        return;
    }
    memset(&comp, 0, sizeof(comp));
    memset(&dir, 0, sizeof(dir));
    comp.schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
    comp.mode = ODM_COMPOSITION_MODE_FLOW;
    comp.tick_index = 137u;
    comp.center_sample = 137 * (int64_t)ODM_MUSIC_TICK_SAMPLES;
    comp.core_scale_q31 = (uint32_t)INT32_MAX / 2u;
    comp.particles_q31 = (uint32_t)INT32_MAX / 3u;
    comp.radial_gain_q31 = (uint32_t)INT32_MAX / 2u;
    for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i)
        comp.radial_q31[i] = (uint32_t)(((uint64_t)INT32_MAX * (i + 1u)) / 96u);
    dir.schema_version = ODM_DIRECTOR_SCHEMA_VERSION;
    dir.tick_index = comp.tick_index;
    dir.layout = ODM_DIRECTOR_LAYOUT_MONOLITH;

    for (layout = 0u; layout <= ODM_FIELD_LAYOUT_MAX; ++layout) {
        odm_layered_config c = base;
        odm_layered_frame_plan p;
        c.field.layout = layout;
        ODM_TEST_CHECK(context, odm_layered_config_validate(&c) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
            odm_layered_resolve_frame_plan(&c, &comp, &dir, 65760, 4800000, &p) == ODM_STATUS_OK);
        if (!primero_listo) { primero = p; primero_listo = 1; continue; }
        /* Todo el plan menos su identidad de configuracion. */
        p.config_sha256 = primero.config_sha256;
        ODM_TEST_CHECK(context, memcmp(&p, &primero, sizeof(p)) == 0);
    }
    /* Y configuraciones distintas siguen teniendo identidades distintas: la
     * propiedad es "misma musica", no "misma configuracion". */
    {
        odm_layered_config a = base, b = base;
        odm_sha256_digest ha, hb;
        a.field.layout = ODM_FIELD_LAYOUT_RADIAL;
        b.field.layout = ODM_FIELD_LAYOUT_GRID;
        ODM_TEST_CHECK(context, odm_layered_config_sha256(&a, &ha) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_config_sha256(&b, &hb) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(&ha, &hb, sizeof(ha)) != 0);
    }
}

/* EL CAMPO NO PINTA SOBRE EL HUD.
 *
 * Las dos capas comparten lienzo y la banda inferior la reserva el HUD. Se
 * comprueba que la reserva del plan cubre de verdad lo que el HUD dibuja: si
 * alguien retoca el HUD y la reserva se queda corta, las barras acaban encima
 * del titulo -- exacto por dentro y descuidado por fuera. */
static void test_banda_del_hud_reservada(odm_test_context *context) {
    odm_layered_config c;
    layer_hud_metrics m;
    int64_t esperado;

    if (odm_layered_config_init_default(&c, ODM_CANVAS_ASPECT_HORIZONTAL_16_9,
                                        1280u, 720u, 30) != ODM_STATUS_OK) {
        ODM_TEST_CHECK(context, 0);
        return;
    }
    c.hud.flags = ODM_HUD_PROGRESS_BAR | ODM_HUD_TIME_CODE | ODM_HUD_TITLE | ODM_HUD_ARTIST;
    c.hud.metadata_anchor = ODM_HUD_ANCHOR_BOTTOM_CENTER;
    memcpy(c.hud.title, "TITULO", 7u);
    memcpy(c.hud.artist, "AUTORIA", 8u);
    odm_layered_hud_metrics(&c, &m);
    /* La linea fina recorta el alto de la barra a un pixel, y la banda tiene
     * que reservar el alto EFECTIVO, no el pedido: reservar de mas dejaria un
     * hueco muerto bajo el campo. */
    ODM_TEST_CHECK(context, c.hud.progress_style == ODM_HUD_PROGRESS_HAIRLINE);
    ODM_TEST_CHECK(context, c.hud.progress_height_q16 > (uint32_t)65536);
    ODM_TEST_CHECK(context, m.progress_h_q16 == 65536);
    /* Reconstruccion independiente de la banda: margen + barra + separacion +
     * codigo de tiempo + separacion + dos lineas de texto con su separacion. */
    esperado = (int64_t)c.hud.margin_q16
             + m.progress_h_q16 + (int64_t)c.hud.line_gap_q16
             + 7 * (int64_t)c.hud.text_scale_q16 + (int64_t)c.hud.line_gap_q16
             + 7 * (int64_t)c.hud.text_scale_q16 + (int64_t)c.hud.line_gap_q16
             + 7 * (int64_t)c.hud.text_scale_q16;
    ODM_TEST_CHECK(context, m.total == esperado);
    /* Un HUD apagado no reserva nada: el campo recupera el cuadro entero. */
    c.hud.flags = 0u;
    odm_layered_hud_metrics(&c, &m);
    ODM_TEST_CHECK(context, m.total == 0);
    /* Quitar el titulo tiene que reducir la banda, no dejarla igual. */
    c.hud.flags = ODM_HUD_PROGRESS_BAR | ODM_HUD_TIME_CODE;
    odm_layered_hud_metrics(&c, &m);
    ODM_TEST_CHECK(context, m.total > 0 && m.total < esperado);
}

void odm_test_compositor(odm_test_context *context) {
    odm_layered_config c, c2;
    odm_composition_frame_state comp;
    odm_director_frame_state dir;
    odm_layered_frame_plan p, p2;
    odm_export_plan ep;
    uint8_t src_pixels[8u * 8u * 4u];
    uint8_t src_linear16[8u * 8u * 8u];
    odm_render_surface_frame surface, surface_linear8, surface_linear16;
    uint64_t fb = 0u, sb = 0u, rf = 0u, rs = 0u;
    uint8_t *scratch = NULL, *out1 = NULL, *out2 = NULL;
    odm_sha256_digest h1, h2, policy;
    uint8_t policy_bytes[ODM_LAYERED_POLICY_BYTES];
    uint64_t policy_required = 0u;
    uint32_t i;

    compositor_config(&c, 32u, 32u, ODM_CANVAS_ASPECT_SQUARE_1_1);
    compositor_states(&comp, &dir);
    fill_surface(src_pixels, &surface);
    ODM_TEST_CHECK(context, odm_layered_config_validate(&c) == ODM_STATUS_OK);

    /* Radial resolution bridge: legacy 48 remains exact, while the new
     * Music-Reaction path can preserve 96 causal radials one-to-one. */
    {
        odm_layered_config hires_cfg = c;
        odm_composition_frame_state hires_comp = comp;
        odm_layered_frame_plan hires_plan, reduced_plan;
        uint32_t j;
        hires_cfg.field.radial_segments = ODM_COMPOSITION_RADIAL_SEGMENTS_MAX;
        ODM_TEST_CHECK(context, odm_layered_config_validate(&hires_cfg) == ODM_STATUS_OK);
        hires_comp.flags |= ODM_COMPOSITION_FLAG_RADIAL_HIRES;
        memset(hires_comp.radial_q31, 0, sizeof(hires_comp.radial_q31));
        for (j = 0u; j < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++j)
            hires_comp.radial_q31[j] = j * UINT32_C(1000000);
        ODM_TEST_CHECK(context,
            odm_layered_resolve_frame_plan(&hires_cfg, &hires_comp, &dir,
                                           24000, 48000, &hires_plan) == ODM_STATUS_OK);
        for (j = 0u; j < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++j)
            ODM_TEST_CHECK(context, hires_plan.radial_q31[j] == hires_comp.radial_q31[j]);
        ODM_TEST_CHECK(context,
            odm_layered_resolve_frame_plan(&c, &hires_comp, &dir,
                                           24000, 48000, &reduced_plan) == ODM_STATUS_OK);
        for (j = 0u; j < ODM_COMPOSITION_RADIAL_SEGMENTS; ++j) {
            uint32_t expected = (uint32_t)(((uint64_t)hires_comp.radial_q31[j * 2u] +
                                            (uint64_t)hires_comp.radial_q31[j * 2u + 1u] + 1u) / 2u);
            ODM_TEST_CHECK(context, reduced_plan.radial_q31[j] == expected);
        }
        for (j = ODM_COMPOSITION_RADIAL_SEGMENTS; j < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++j)
            ODM_TEST_CHECK(context, reduced_plan.radial_q31[j] == 0u);

        hires_comp.flags |= ODM_COMPOSITION_FLAG_STRICT_CAUSAL;
        ODM_TEST_CHECK(context,
            odm_layered_resolve_frame_plan(&c, &hires_comp, &dir,
                                           24000, 48000, &reduced_plan) == ODM_STATUS_OK);
        for (j = 0u; j < ODM_COMPOSITION_RADIAL_SEGMENTS; ++j) {
            uint32_t a = hires_comp.radial_q31[j * 2u];
            uint32_t b = hires_comp.radial_q31[j * 2u + 1u];
            ODM_TEST_CHECK(context, reduced_plan.radial_q31[j] == (a > b ? a : b));
        }
    }


    /* STRICT_CAUSAL is independent from radial resolution. It explicitly
     * freezes autonomous grid/ring phase and propagates as a render authority.
     * HIRES alone remains a resolution capability and does not imply strict
     * causality. */
    {
        odm_layered_config hc = c;
        odm_composition_frame_state strict_a = comp, strict_b = comp, hires_only = comp;
        odm_director_frame_state da = dir, db = dir;
        odm_layered_frame_plan pa, pb, ph;
        hc.field.radial_segments = ODM_COMPOSITION_RADIAL_SEGMENTS_MAX;
        strict_a.flags = ODM_COMPOSITION_FLAG_RADIAL_HIRES | ODM_COMPOSITION_FLAG_STRICT_CAUSAL;
        strict_b = strict_a;
        strict_a.phase = UINT32_C(0x11111111); strict_a.ring_phase = UINT32_C(0x22222222);
        strict_b.phase = UINT32_C(0xeeeeeeee); strict_b.ring_phase = UINT32_C(0xdddddddd);
        da.orbit_phase = UINT32_C(0x33333333); db.orbit_phase = UINT32_C(0xcccccccc);
        ODM_TEST_CHECK(context, odm_layered_resolve_frame_plan(&hc, &strict_a, &da,
                                                               24000, 48000, &pa) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_resolve_frame_plan(&hc, &strict_b, &db,
                                                               24000, 48000, &pb) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, pa.grid_offset_x_q16 == 0 && pa.grid_offset_y_q16 == 0);
        ODM_TEST_CHECK(context, pa.ring_phase == 0u);
        ODM_TEST_CHECK(context, (pa.flags & ODM_COMPOSITION_FLAG_STRICT_CAUSAL) != 0u);
        ODM_TEST_CHECK(context, memcmp(&pa, &pb, sizeof(pa)) == 0);

        hires_only.flags = ODM_COMPOSITION_FLAG_RADIAL_HIRES;
        hires_only.phase = UINT32_C(0x11111111);
        hires_only.ring_phase = UINT32_C(0x22222222);
        ODM_TEST_CHECK(context, odm_layered_resolve_frame_plan(&hc, &hires_only, &da,
                                                               24000, 48000, &ph) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, (ph.flags & ODM_COMPOSITION_FLAG_STRICT_CAUSAL) == 0u);
        ODM_TEST_CHECK(context, ph.ring_phase == hires_only.ring_phase);
        ODM_TEST_CHECK(context, ph.grid_offset_x_q16 != 0 || ph.grid_offset_y_q16 != 0);
    }


    /* Style Profiles are reusable authoring records, never hidden render state.
     * Applying one produces an ordinary Layered Config and supports exact
     * per-layer independence through layer_mask. */
    {
        odm_layered_style_profile style, style2, invalid;
        odm_layered_config base, styled, styled2, sentinel;
        odm_sha256_digest sh1, sh2, ch_base, ch_styled;
        uint8_t sbuf[ODM_LAYERED_STYLE_BYTES];
        uint64_t sreq = 0u;
        uint32_t preset;
        ODM_TEST_CHECK(context,
                       odm_layered_config_init_default(&base,
                           ODM_CANVAS_ASPECT_SQUARE_1_1, 1080u, 1080u, 30) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_layered_config_set_metadata(&base, "Quiet, Not Empty", "AFTERIMAGE") == ODM_STATUS_OK);
        for (preset = ODM_LAYERED_STYLE_PRESET_MONO_PRECISE;
             preset <= ODM_LAYERED_STYLE_PRESET_MINIMAL_EDITORIAL; ++preset) {
            ODM_TEST_CHECK(context, odm_layered_style_init(&style, preset) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_layered_style_validate(&style) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           odm_layered_style_bytes(&style, sbuf, sizeof(sbuf), &sreq) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, sreq == ODM_LAYERED_STYLE_BYTES);
            ODM_TEST_CHECK(context, odm_layered_style_sha256(&style, &sh1) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_sha256(sbuf, sizeof(sbuf), &sh2) == ODM_STATUS_OK &&
                                    odm_sha256_equal(&sh1, &sh2));
            ODM_TEST_CHECK(context, odm_layered_style_apply(&base, &style, &styled) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_layered_config_validate(&styled) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, styled.canvas.aspect == base.canvas.aspect &&
                                    styled.canvas.width == base.canvas.width &&
                                    styled.canvas.height == base.canvas.height &&
                                    styled.canvas.fps == base.canvas.fps);
            ODM_TEST_CHECK(context, styled.core.shape == base.core.shape &&
                                    strcmp(styled.hud.title, base.hud.title) == 0 &&
                                    strcmp(styled.hud.artist, base.hud.artist) == 0);
            ODM_TEST_CHECK(context, odm_layered_style_apply(&base, &style, &styled2) == ODM_STATUS_OK &&
                                    memcmp(&styled, &styled2, sizeof(styled)) == 0);
        }
        ODM_TEST_CHECK(context,
                       odm_layered_style_init(&style, ODM_LAYERED_STYLE_PRESET_VOID_CINEMATIC) == ODM_STATUS_OK);
        style.layer_mask = ODM_LAYERED_STYLE_LAYER_BACKGROUND;
        ODM_TEST_CHECK(context, odm_layered_style_apply(&base, &style, &styled) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(&styled.core, &base.core, sizeof(base.core)) == 0 &&
                                memcmp(&styled.field, &base.field, sizeof(base.field)) == 0 &&
                                memcmp(&styled.hud, &base.hud, sizeof(base.hud)) == 0);
        ODM_TEST_CHECK(context, memcmp(&styled.background, &base.background, sizeof(base.background)) != 0);
        ODM_TEST_CHECK(context, odm_layered_config_sha256(&base, &ch_base) == ODM_STATUS_OK &&
                                odm_layered_config_sha256(&styled, &ch_styled) == ODM_STATUS_OK &&
                                !odm_sha256_equal(&ch_base, &ch_styled));
        invalid = style; invalid.grid_spacing_scale_q16 = 0u;
        sentinel = base; styled2 = sentinel;
        ODM_TEST_CHECK(context, odm_layered_style_validate(&invalid) == ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(context, odm_layered_style_apply(&base, &invalid, &styled2) == ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(context, memcmp(&styled2, &sentinel, sizeof(sentinel)) == 0);
        style2 = style; style2.layer_mask = 0u; styled2 = sentinel;
        ODM_TEST_CHECK(context, odm_layered_style_apply(&base, &style2, &styled2) == ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(context, memcmp(&styled2, &sentinel, sizeof(sentinel)) == 0);
    }

    /* Reaction Matrix is canonical authoring state. Legacy all-zero routing
     * preserves 0.13 geometry, while explicit routes alter config identity and
     * only the targeted resolved quantities. */
    {
        odm_layered_config legacy, explicit_cfg, changed, before;
        odm_layered_frame_plan legacy_plan, explicit_plan, changed_plan;
        odm_sha256_digest legacy_sha, explicit_sha, changed_sha;
        odm_reaction_config def;
        uint32_t route = UINT32_C(0xdeadbeef), invalid_route;
        compositor_config(&legacy, 32u, 32u, ODM_CANVAS_ASPECT_SQUARE_1_1);
        ODM_TEST_CHECK(context, memcmp(&legacy.reaction, &(odm_reaction_config){0},
                                       sizeof(legacy.reaction)) == 0);
        explicit_cfg = legacy;
        ODM_TEST_CHECK(context, odm_layered_reaction_init_default(&def) == ODM_STATUS_OK);
        explicit_cfg.reaction = def;
        ODM_TEST_CHECK(context, odm_layered_config_validate(&explicit_cfg) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_resolve_frame_plan(&legacy, &comp, &dir,
                                                               24000, 48000, &legacy_plan) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_resolve_frame_plan(&explicit_cfg, &comp, &dir,
                                                               24000, 48000, &explicit_plan) == ODM_STATUS_OK);
        {
            odm_sha256_digest a = legacy_plan.config_sha256;
            odm_sha256_digest b = explicit_plan.config_sha256;
            memset(&legacy_plan.config_sha256, 0, sizeof(legacy_plan.config_sha256));
            memset(&explicit_plan.config_sha256, 0, sizeof(explicit_plan.config_sha256));
            ODM_TEST_CHECK(context, memcmp(&legacy_plan, &explicit_plan, sizeof(legacy_plan)) == 0);
            legacy_plan.config_sha256 = a;
            explicit_plan.config_sha256 = b;
        }
        ODM_TEST_CHECK(context, odm_layered_config_sha256(&legacy, &legacy_sha) == ODM_STATUS_OK &&
                                odm_layered_config_sha256(&explicit_cfg, &explicit_sha) == ODM_STATUS_OK &&
                                !odm_sha256_equal(&legacy_sha, &explicit_sha));

        ODM_TEST_CHECK(context, odm_reaction_route_make(ODM_REACTION_SOURCE_BAND_0,
                                                        ODM_REACTION_SOURCE_NONE,
                                                        ODM_REACTION_COMBINE_A_ONLY,
                                                        &route) == ODM_STATUS_OK);
        changed = explicit_cfg;
        comp.band_q31[0] = (uint32_t)INT32_MAX;
        comp.grid_q31 = 0u;
        ODM_TEST_CHECK(context, odm_layered_config_set_reaction_route(&changed,
                                ODM_REACTION_TARGET_BACKGROUND_ZOOM, route) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_resolve_frame_plan(&changed, &comp, &dir,
                                                               24000, 48000, &changed_plan) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, changed_plan.grid_spacing_q16 > legacy_plan.grid_spacing_q16);
        ODM_TEST_CHECK(context, odm_layered_config_sha256(&changed, &changed_sha) == ODM_STATUS_OK &&
                                !odm_sha256_equal(&explicit_sha, &changed_sha));

        /* Core scale may be driven entirely from the Director. */
        changed = explicit_cfg;
        comp.core_breath_q31 = 0u;
        dir.macro_energy_q31 = (uint32_t)INT32_MAX;
        ODM_TEST_CHECK(context, odm_reaction_route_make(ODM_REACTION_SOURCE_DIRECTOR_ENERGY,
                                                        ODM_REACTION_SOURCE_NONE,
                                                        ODM_REACTION_COMBINE_A_ONLY,
                                                        &route) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_config_set_reaction_route(&changed,
                                ODM_REACTION_TARGET_CORE_SCALE, route) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_resolve_frame_plan(&changed, &comp, &dir,
                                                               24000, 48000, &changed_plan) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, changed_plan.core_half_w_q16 > explicit_plan.core_half_w_q16);

        /* NONE/A_ONLY is the canonical explicit disable route. */
        changed = explicit_cfg;
        ODM_TEST_CHECK(context, odm_reaction_route_make(ODM_REACTION_SOURCE_NONE,
                                                        ODM_REACTION_SOURCE_NONE,
                                                        ODM_REACTION_COMBINE_A_ONLY,
                                                        &route) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_config_set_reaction_route(&changed,
                                ODM_REACTION_TARGET_FIELD_GAIN, route) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_resolve_frame_plan(&changed, &comp, &dir,
                                                               24000, 48000, &changed_plan) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, changed_plan.field_opacity_q31 == 0u);

        /* Particle density can be routed from a single exact band. */
        changed = explicit_cfg;
        comp.band_q31[5] = 0u;
        ODM_TEST_CHECK(context, odm_reaction_route_make(ODM_REACTION_SOURCE_BAND_5,
                                                        ODM_REACTION_SOURCE_NONE,
                                                        ODM_REACTION_COMBINE_A_ONLY,
                                                        &route) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_config_set_reaction_route(&changed,
                                ODM_REACTION_TARGET_PARTICLE_DENSITY, route) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_resolve_frame_plan(&changed, &comp, &dir,
                                                               24000, 48000, &changed_plan) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, changed_plan.particle_count == 2u); /* 1/4 of configured 8 */
        comp.band_q31[5] = (uint32_t)INT32_MAX;
        ODM_TEST_CHECK(context, odm_layered_resolve_frame_plan(&changed, &comp, &dir,
                                                               24000, 48000, &changed_plan) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, changed_plan.particle_count == 8u);

        /* Binary combines require two real sources; high/reserved bits fail. */
        before = changed;
        route = UINT32_C(0x12345678);
        ODM_TEST_CHECK(context, odm_reaction_route_make(ODM_REACTION_SOURCE_GRID,
                                                        ODM_REACTION_SOURCE_NONE,
                                                        ODM_REACTION_COMBINE_MAX,
                                                        &route) == ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context, route == UINT32_C(0x12345678));
        invalid_route = ODM_REACTION_SOURCE_GRID | UINT32_C(0x80000000);
        ODM_TEST_CHECK(context, odm_layered_config_set_reaction_route(&changed,
                                ODM_REACTION_TARGET_BACKGROUND_ZOOM, invalid_route) == ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context, memcmp(&changed, &before, sizeof(before)) == 0);
        changed = explicit_cfg;
        changed.reaction.background_zoom_route |= UINT32_C(0x00800000);
        ODM_TEST_CHECK(context, odm_layered_config_validate(&changed) == ODM_STATUS_INVALID_DATA);

        /* Style authoring never mutates routing. */
        {
            odm_layered_style_profile style;
            odm_layered_config styled;
            ODM_TEST_CHECK(context, odm_layered_style_init(&style,
                                   ODM_LAYERED_STYLE_PRESET_DEEP_GRID) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_layered_style_apply(&explicit_cfg, &style, &styled) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, memcmp(&styled.reaction, &explicit_cfg.reaction,
                                           sizeof(styled.reaction)) == 0);
        }
        compositor_states(&comp, &dir); /* restore canonical fixture */
    }

    /* Canonical constructors remove host-side hidden knowledge. They are
     * transactional and materialize ordinary hashed Layered Config records. */
    {
        odm_layered_config preset, preset2, before;
        ODM_TEST_CHECK(context,
                       odm_layered_config_init_default(&preset,
                           ODM_CANVAS_ASPECT_HORIZONTAL_16_9, 1920u, 1080u, 30) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_config_validate(&preset) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, preset.canvas.width == 1920u && preset.canvas.height == 1080u);
        ODM_TEST_CHECK(context, preset.background.style == ODM_BACKGROUND_PERSPECTIVE_GRID);
        ODM_TEST_CHECK(context, preset.core.shape == ODM_CORE_SHAPE_CIRCLE);
        ODM_TEST_CHECK(context, (preset.hud.flags & (ODM_HUD_PROGRESS_BAR|ODM_HUD_TIME_CODE)) ==
                                (ODM_HUD_PROGRESS_BAR|ODM_HUD_TIME_CODE));
        ODM_TEST_CHECK(context, (preset.hud.flags & (ODM_HUD_TITLE|ODM_HUD_ARTIST)) == 0u);
        ODM_TEST_CHECK(context,
                       odm_layered_config_init_default(&preset2,
                           ODM_CANVAS_ASPECT_HORIZONTAL_16_9, 1920u, 1080u, 30) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(&preset, &preset2, sizeof(preset)) == 0);
        ODM_TEST_CHECK(context,
                       odm_layered_config_init_default(&preset2,
                           ODM_CANVAS_ASPECT_SQUARE_1_1, 1080u, 1080u, 30) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_config_validate(&preset2) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_layered_config_init_default(&preset2,
                           ODM_CANVAS_ASPECT_VERTICAL_9_16, 1080u, 1920u, 30) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_config_validate(&preset2) == ODM_STATUS_OK);
        before = preset2;
        ODM_TEST_CHECK(context,
                       odm_layered_config_init_default(&preset2,
                           ODM_CANVAS_ASPECT_VERTICAL_9_16, 1080u, 1919u, 30) == ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context, memcmp(&preset2, &before, sizeof(before)) == 0);
        ODM_TEST_CHECK(context, odm_layered_config_set_core_shape(&preset, ODM_CORE_SHAPE_SQUARE) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, preset.core.shape == ODM_CORE_SHAPE_SQUARE);
        ODM_TEST_CHECK(context, odm_layered_config_set_core_shape(&preset, ODM_CORE_SHAPE_ROUNDED_RECT) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, preset.core.shape == ODM_CORE_SHAPE_ROUNDED_RECT);
        before = preset;
        ODM_TEST_CHECK(context, odm_layered_config_set_core_shape(&preset, 99u) == ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context, memcmp(&preset, &before, sizeof(before)) == 0);
        ODM_TEST_CHECK(context, odm_layered_config_set_metadata(&preset, "Quiet, Not Empty", "AFTERIMAGE") == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, (preset.hud.flags & ODM_HUD_TITLE) != 0u &&
                                (preset.hud.flags & ODM_HUD_ARTIST) != 0u);
        ODM_TEST_CHECK(context, strcmp(preset.hud.title, "Quiet, Not Empty") == 0 &&
                                strcmp(preset.hud.artist, "AFTERIMAGE") == 0);
        before = preset;
        {
            char too_long[ODM_LAYERED_METADATA_BYTES + 2u];
            memset(too_long, 'A', sizeof(too_long));
            too_long[sizeof(too_long)-1u] = '\0';
            ODM_TEST_CHECK(context, odm_layered_config_set_metadata(&preset, too_long, "AFTERIMAGE") == ODM_STATUS_INVALID_DATA);
        }
        ODM_TEST_CHECK(context, memcmp(&preset, &before, sizeof(before)) == 0);
        ODM_TEST_CHECK(context, odm_layered_config_set_metadata(&preset, "", "") == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, (preset.hud.flags & (ODM_HUD_TITLE|ODM_HUD_ARTIST)) == 0u);
    }
    /* Exhaustive fast delivery LUT equivalence: every possible linear16 code
     * must equal the canonical renderer conversion. */
    for (i = 0u; i < 65536u; ++i) {
        ODM_TEST_CHECK(context,
                       odm_layered_linear_u16_to_srgb8_fast((uint16_t)i) ==
                       odm_renderer_linear_u16_to_srgb8((uint16_t)i));
    }

    c2 = c; c2.canvas.width = 160u; c2.canvas.height = 90u;
    c2.canvas.aspect = ODM_CANVAS_ASPECT_HORIZONTAL_16_9;
    ODM_TEST_CHECK(context, odm_layered_config_validate(&c2) == ODM_STATUS_OK);
    c2.canvas.width = 90u; c2.canvas.height = 160u;
    c2.canvas.aspect = ODM_CANVAS_ASPECT_VERTICAL_9_16;
    ODM_TEST_CHECK(context, odm_layered_config_validate(&c2) == ODM_STATUS_OK);
    c2.canvas.width = 100u;
    ODM_TEST_CHECK(context, odm_layered_config_validate(&c2) == ODM_STATUS_INVALID_DATA);

    ODM_TEST_CHECK(context,
                   odm_layered_resolve_frame_plan(&c, &comp, &dir, 24000, 48000, &p) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, p.width == 32u && p.height == 32u && p.fps == 30);
    ODM_TEST_CHECK(context, p.center_x_q16 == (16 << 16) && p.center_y_q16 == (16 << 16));
    ODM_TEST_CHECK(context, p.core_half_w_q16 == p.core_half_h_q16);
    ODM_TEST_CHECK(context, p.progress_q31 == q31_ratio(1u, 2u));
    ODM_TEST_CHECK(context, p.safe_left_q16 > 0 && p.safe_top_q16 > 0 &&
                            p.safe_right_q16 < (32 << 16) && p.safe_bottom_q16 < (32 << 16));
    ODM_TEST_CHECK(context, p.particle_count == 5u);
    ODM_TEST_CHECK(context, p.particle_count <= c.field.particle_count);

    /* Progress ratio remains exact over the full admitted int64 timeline, not
     * only where sample*Q31 happens to fit uint64_t. */
    {
        odm_layered_frame_plan pext;
        const int64_t dmax = INT64_MAX;
        ODM_TEST_CHECK(context,
                       odm_layered_resolve_frame_plan(&c,&comp,&dir,0,dmax,&pext)==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,pext.progress_q31==0u);
        ODM_TEST_CHECK(context,
                       odm_layered_resolve_frame_plan(&c,&comp,&dir,dmax/2,dmax,&pext)==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,pext.progress_q31==UINT32_C(1073741823));
        ODM_TEST_CHECK(context,
                       odm_layered_resolve_frame_plan(&c,&comp,&dir,dmax-1,dmax,&pext)==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,pext.progress_q31==(uint32_t)INT32_MAX);
        ODM_TEST_CHECK(context,
                       odm_layered_resolve_frame_plan(&c,&comp,&dir,dmax,dmax,&pext)==ODM_STATUS_OK);
        ODM_TEST_CHECK(context,pext.progress_q31==(uint32_t)INT32_MAX);
    }
    ODM_TEST_CHECK(context, p.layout == ODM_DIRECTOR_LAYOUT_ORBIT);

    /* HUD metadata is independent of layout resolution. */
    c2 = c;
    memset(c2.hud.title, 0, sizeof(c2.hud.title));
    memcpy(c2.hud.title, "ALT", 4u);
    ODM_TEST_CHECK(context,
                   odm_layered_resolve_frame_plan(&c2, &comp, &dir, 24000, 48000, &p2) == ODM_STATUS_OK);
    {
        odm_layered_frame_plan a = p, b = p2;
        memset(&a.config_sha256, 0, sizeof(a.config_sha256));
        memset(&b.config_sha256, 0, sizeof(b.config_sha256));
        ODM_TEST_CHECK(context, memcmp(&a, &b, sizeof(a)) == 0);
        ODM_TEST_CHECK(context, !odm_sha256_equal(&p.config_sha256, &p2.config_sha256));
    }

    ODM_TEST_CHECK(context,
                   odm_layered_render_requirements(&c, ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                   &fb, &sb) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, fb == UINT64_C(32) * 32u * 4u);
    /* Scratch = canonical RGBA16 work + encoded RGBA8 staging + one
     * 8-byte Core x-map entry + one 4-byte Background column-warp entry per
     * output column. Both caches are per-frame derived state, never authority. */
    ODM_TEST_CHECK(context, sb == UINT64_C(32) * 32u * 12u + UINT64_C(32) * 12u);
    scratch = (uint8_t *)aligned_alloc(8u, (size_t)sb);
    out1 = (uint8_t *)malloc((size_t)fb);
    out2 = (uint8_t *)malloc((size_t)fb);
    ODM_TEST_CHECK(context, scratch != NULL && out1 != NULL && out2 != NULL);
    if (scratch && out1 && out2) {
        ODM_TEST_CHECK(context,
                       odm_layered_render_frame(&c, &p, &surface, NULL,
                                                ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                scratch, sb, out1, fb, &rf, &rs, &h1) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, rf == fb && rs == sb);
        ODM_TEST_CHECK(context,
                       odm_layered_render_frame(&c, &p, &surface, NULL,
                                                ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                scratch, sb, out2, fb, &rf, &rs, &h2) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(out1, out2, (size_t)fb) == 0 &&
                                odm_sha256_equal(&h1, &h2));


        /* Independent layer outputs must equal the exact full compositor when
         * all other authorities are disabled. This avoids claiming integer
         * alpha-over associativity across flattened layers, while proving each
         * exported layer uses the same semantic raster as production. */
        {
            uint64_t lfb=0u,lsb=0u,f16b=0u,f16s=0u,rr=0u,rrs=0u,px;
            uint8_t *lscratch=NULL,*fullscratch=NULL,*layer=NULL,*isolated=NULL;
            odm_sha256_digest lh,ih;
            uint32_t li;
            ODM_TEST_CHECK(context,odm_layered_render_layer_requirements(&c,&lfb,&lsb)==ODM_STATUS_OK);
            ODM_TEST_CHECK(context,lfb==UINT64_C(32)*32u*8u&&lsb==lfb*2u);
            ODM_TEST_CHECK(context,odm_layered_render_requirements(&c,
                ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,&f16b,&f16s)==ODM_STATUS_OK);
            lscratch=(uint8_t*)aligned_alloc(8u,(size_t)lsb);
            fullscratch=(uint8_t*)aligned_alloc(8u,(size_t)f16s);
            layer=(uint8_t*)malloc((size_t)lfb);isolated=(uint8_t*)malloc((size_t)f16b);
            ODM_TEST_CHECK(context,lscratch&&fullscratch&&layer&&isolated);
            if(lscratch&&fullscratch&&layer&&isolated){
                for(li=0u;li<4u;++li){
                    uint32_t lid=ODM_LAYERED_LAYER_BACKGROUND+li;
                    odm_layered_config only=c;
                    odm_layered_frame_plan op;
                    if(lid!=ODM_LAYERED_LAYER_BACKGROUND)only.background.style=ODM_BACKGROUND_NONE;
                    if(lid!=ODM_LAYERED_LAYER_CORE)only.core.opacity_q31=0u;
                    if(lid!=ODM_LAYERED_LAYER_FIELD)only.field.flags=0u;
                    if(lid!=ODM_LAYERED_LAYER_HUD)only.hud.flags=0u;
                    ODM_TEST_CHECK(context,odm_layered_config_validate(&only)==ODM_STATUS_OK);
                    ODM_TEST_CHECK(context,odm_layered_resolve_frame_plan(&only,&comp,&dir,24000,48000,&op)==ODM_STATUS_OK);
                    ODM_TEST_CHECK(context,odm_layered_render_layer_rgba16(&c,&p,&surface,lid,NULL,
                        lscratch,lsb,layer,lfb,&rr,&rrs,&lh)==ODM_STATUS_OK);
                    ODM_TEST_CHECK(context,odm_layered_render_frame(&only,&op,&surface,NULL,
                        ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,fullscratch,f16s,
                        isolated,f16b,&rr,&rrs,&ih)==ODM_STATUS_OK);
                    ODM_TEST_CHECK(context,memcmp(layer,isolated,(size_t)lfb)==0&&odm_sha256_equal(&lh,&ih));
                }
                memset(layer,0xA5,(size_t)lfb);
                ODM_TEST_CHECK(context,odm_layered_render_layer_rgba16(&c,&p,&surface,99u,NULL,
                    lscratch,lsb,layer,lfb,&rr,&rrs,&lh)==ODM_STATUS_INVALID_ARGUMENT);
                for(px=0u;px<lfb;++px)if(layer[px]!=UINT8_C(0xA5))break;
                ODM_TEST_CHECK(context,px==lfb);
            }
            free(isolated);free(layer);free(fullscratch);free(lscratch);
        }

        /* Canonical Layer Stack composes original authorities rather than
         * alpha-overing already-flattened Layer Outputs. One-bit masks must
         * equal standalone outputs, and ALL must equal the monolithic full
         * compositor byte-for-byte and SHA-for-SHA. */
        {
            uint64_t lfb=0u,lsb=0u,f16b=0u,f16s=0u,rr=0u,rrs=0u,px;
            uint8_t *lscratch=NULL,*fullscratch=NULL,*layer=NULL,*stacked=NULL;
            odm_sha256_digest lh,sh,fh;
            uint32_t li;
            static const uint32_t masks[4] = {
                ODM_LAYERED_LAYER_MASK_BACKGROUND, ODM_LAYERED_LAYER_MASK_CORE,
                ODM_LAYERED_LAYER_MASK_FIELD, ODM_LAYERED_LAYER_MASK_HUD
            };
            ODM_TEST_CHECK(context,odm_layered_render_layer_requirements(&c,&lfb,&lsb)==ODM_STATUS_OK);
            ODM_TEST_CHECK(context,odm_layered_render_requirements(&c,
                ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,&f16b,&f16s)==ODM_STATUS_OK);
            ODM_TEST_CHECK(context,lfb==f16b);
            lscratch=(uint8_t*)aligned_alloc(8u,(size_t)lsb);
            fullscratch=(uint8_t*)aligned_alloc(8u,(size_t)f16s);
            layer=(uint8_t*)malloc((size_t)lfb);stacked=(uint8_t*)malloc((size_t)f16b);
            ODM_TEST_CHECK(context,lscratch&&fullscratch&&layer&&stacked);
            if(lscratch&&fullscratch&&layer&&stacked){
                for(li=0u;li<4u;++li){
                    uint32_t lid=ODM_LAYERED_LAYER_BACKGROUND+li;
                    const odm_render_surface_frame *stack_surface =
                        masks[li]==ODM_LAYERED_LAYER_MASK_CORE ? &surface : NULL;
                    ODM_TEST_CHECK(context,odm_layered_render_layer_rgba16(&c,&p,&surface,lid,NULL,
                        lscratch,lsb,layer,lfb,&rr,&rrs,&lh)==ODM_STATUS_OK);
                    ODM_TEST_CHECK(context,odm_layered_render_stack_rgba16(&c,&p,stack_surface,masks[li],NULL,
                        fullscratch,f16s,stacked,f16b,&rr,&rrs,&sh)==ODM_STATUS_OK);
                    ODM_TEST_CHECK(context,memcmp(layer,stacked,(size_t)f16b)==0&&
                                           odm_sha256_equal(&lh,&sh));
                }
                ODM_TEST_CHECK(context,odm_layered_render_frame(&c,&p,&surface,NULL,
                    ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL,fullscratch,f16s,
                    layer,f16b,&rr,&rrs,&fh)==ODM_STATUS_OK);
                ODM_TEST_CHECK(context,odm_layered_render_stack_rgba16(&c,&p,&surface,
                    ODM_LAYERED_LAYER_MASK_ALL,NULL,fullscratch,f16s,stacked,f16b,
                    &rr,&rrs,&sh)==ODM_STATUS_OK);
                ODM_TEST_CHECK(context,memcmp(layer,stacked,(size_t)f16b)==0&&
                                       odm_sha256_equal(&fh,&sh));
                memset(stacked,0xA5,(size_t)f16b);
                ODM_TEST_CHECK(context,odm_layered_render_stack_rgba16(&c,&p,NULL,0u,NULL,
                    fullscratch,f16s,stacked,f16b,&rr,&rrs,&sh)==ODM_STATUS_INVALID_ARGUMENT);
                for(px=0u;px<f16b;++px)if(stacked[px]!=UINT8_C(0xA5))break;
                ODM_TEST_CHECK(context,px==f16b);
                ODM_TEST_CHECK(context,odm_layered_render_stack_rgba16(&c,&p,NULL,UINT32_C(16),NULL,
                    fullscratch,f16s,stacked,f16b,&rr,&rrs,&sh)==ODM_STATUS_INVALID_ARGUMENT);
            }
            free(stacked);free(layer);free(fullscratch);free(lscratch);
        }

        /* Native Export may omit the redundant per-frame digest because it
         * binds every returned byte into video_stream_sha256. The fast path
         * must remain byte-identical to the public hashed renderer. */
        memset(out2, 0xA5, (size_t)fb);
        ODM_TEST_CHECK(context,
                       odm_layered_render_frame_stream_unhashed(
                           &c, &p, &surface, NULL,
                           ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                           scratch, sb, out2, fb, &rf, &rs) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(out1, out2, (size_t)fb) == 0);
        ODM_TEST_CHECK(context, odm_sha256(out2, fb, &h2) == ODM_STATUS_OK &&
                                odm_sha256_equal(&h1, &h2));
        {
            const uint8_t *stage_ptr = NULL;
            ODM_TEST_CHECK(context,
                           odm_layered_render_frame_stream_stage_unhashed(
                               &c, &p, &surface, NULL,
                               ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                               scratch, sb, &stage_ptr, &rf, &rs) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, stage_ptr != NULL && rf == fb && rs == sb);
            ODM_TEST_CHECK(context, memcmp(out1, stage_ptr, (size_t)fb) == 0);
            {
                const uint8_t *stage2 = NULL, *stage4 = NULL;
                ODM_TEST_CHECK(context,
                    odm_layered_render_frame_stream_stage_unhashed_workers(
                        &c,&p,&surface,NULL,
                        ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                        scratch,sb,2u,&stage2,&rf,&rs)==ODM_STATUS_OK);
                ODM_TEST_CHECK(context,stage2!=NULL&&memcmp(out1,stage2,(size_t)fb)==0);
                ODM_TEST_CHECK(context,
                    odm_layered_render_frame_stream_stage_unhashed_workers(
                        &c,&p,&surface,NULL,
                        ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                        scratch,sb,4u,&stage4,&rf,&rs)==ODM_STATUS_OK);
                ODM_TEST_CHECK(context,stage4!=NULL&&memcmp(out1,stage4,(size_t)fb)==0);
            }
        }

        /* Admitted renderer is an internal performance path, never a second
         * semantic path. Full public validation/hash once at admission must
         * produce bytes identical to the defensive public renderer; a plan
         * bound to any other config identity must still fail closed. */
        {
            odm_layered_render_admission admission;
            const uint8_t *stage_admitted = NULL;
            odm_layered_frame_plan bad_plan;
            ODM_TEST_CHECK(context,
                odm_layered_render_frame(&c, &p, &surface, NULL,
                    ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                    scratch, sb, out1, fb, &rf, &rs, &h1) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                odm_layered_render_admit(&c,
                    ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                    &admission) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, admission.frame_bytes == fb &&
                                    admission.scratch_bytes == sb &&
                                    odm_sha256_equal(&admission.config_sha256,
                                                     &p.config_sha256));
            ODM_TEST_CHECK(context,
                odm_layered_render_frame_stream_stage_admitted_workers(
                    &c, &admission, &p, &surface, NULL, scratch, sb, 4u,
                    &stage_admitted, &rf, &rs) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, stage_admitted != NULL &&
                                    memcmp(out1, stage_admitted, (size_t)fb) == 0);
            bad_plan = p;
            bad_plan.config_sha256.bytes[0] ^= UINT8_C(1);
            stage_admitted = NULL;
            ODM_TEST_CHECK(context,
                odm_layered_render_frame_stream_stage_admitted_workers(
                    &c, &admission, &bad_plan, &surface, NULL, scratch, sb, 1u,
                    &stage_admitted, &rf, &rs) == ODM_STATUS_INTEGRITY_ERROR);
            ODM_TEST_CHECK(context, stage_admitted == NULL);
        }

        /* Cached Background column warp is only a performance transform.
         * Compare the complete canonical RGBA16 raster against the scalar
         * phase-carry path, not against a tolerance. */
        {
            odm_layered_pixel16 *br_ref = (odm_layered_pixel16 *)calloc(
                UINT64_C(32) * 32u, sizeof(*br_ref));
            odm_layered_pixel16 *br_cached = (odm_layered_pixel16 *)calloc(
                UINT64_C(32) * 32u, sizeof(*br_cached));
            int32_t shifts[32];
            ODM_TEST_CHECK(context, br_ref != NULL && br_cached != NULL);
            if (br_ref && br_cached) {
                odm_layered_background_prepare_columns(&p, shifts, 32u);
                odm_layered_background_render_rows(&c, &p, 0u, 32u, br_ref);
                odm_layered_background_render_rows_cached(&c, &p, shifts,
                                                          0u, 32u, br_cached);
                ODM_TEST_CHECK(context, memcmp(br_ref, br_cached,
                                               UINT64_C(32) * 32u *
                                               sizeof(*br_ref)) == 0);
            }
            free(br_ref);
            free(br_cached);
        }

        /* Scratch sizing is part of the public transactional contract. One
         * byte below the exact requirement must fail before publishing any
         * frame bytes. */
        if (sb > 0u) {
            memset(out2, 0xA5, (size_t)fb);
            ODM_TEST_CHECK(context,
                           odm_layered_render_frame(&c, &p, &surface, NULL,
                                                    ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                    scratch, sb - 1u, out2, fb, &rf, &rs, &h2) ==
                               ODM_STATUS_BUFFER_TOO_SMALL);
            {
                uint64_t k;
                int unchanged = 1;
                for (k = 0u; k < fb; ++k) {
                    if (out2[k] != UINT8_C(0xA5)) { unchanged = 0; break; }
                }
                ODM_TEST_CHECK(context, unchanged);
            }
            ODM_TEST_CHECK(context, rf == fb && rs == sb);
        }

        /* Canonical Core fast-path: an RGBA8 LINEAR/straight source and its
         * byte-equivalent RGBA16LE LINEAR/premultiplied representation must
         * produce exactly the same final frame.  This permits upstream decode
         * stages to hand canonical pixels to the compositor without repeated
         * transfer/premultiply work. */
        surface_linear8 = surface;
        surface_linear8.transfer = ODM_COLOR_TRANSFER_LINEAR;
        surface_linear16 = surface;
        surface_linear16.pixel_format = ODM_RENDER_SURFACE_RGBA16LE_LINEAR_PREMUL;
        surface_linear16.transfer = ODM_COLOR_TRANSFER_LINEAR;
        surface_linear16.alpha_mode = ODM_ALPHA_PREMULTIPLIED;
        surface_linear16.pixel_bytes = sizeof(src_linear16);
        surface_linear16.pixels = src_linear16;
        for (i = 0u; i < 64u; ++i) {
            uint32_t ch;
            for (ch = 0u; ch < 4u; ++ch) {
                uint16_t v = (uint16_t)((uint16_t)src_pixels[i * 4u + ch] * UINT16_C(257));
                src_linear16[i * 8u + ch * 2u + 0u] = (uint8_t)v;
                src_linear16[i * 8u + ch * 2u + 1u] = (uint8_t)(v >> 8);
            }
        }
        ODM_TEST_CHECK(context, odm_layered_core_surface_validate(&surface_linear8) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_core_surface_validate(&surface_linear16) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_layered_render_frame(&c, &p, &surface_linear8, NULL,
                                                ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                scratch, sb, out1, fb, &rf, &rs, &h1) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_layered_render_frame(&c, &p, &surface_linear16, NULL,
                                                ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                scratch, sb, out2, fb, &rf, &rs, &h2) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(out1, out2, (size_t)fb) == 0 &&
                                odm_sha256_equal(&h1, &h2));
        surface_linear16.alpha_mode = ODM_ALPHA_STRAIGHT;
        ODM_TEST_CHECK(context, odm_layered_core_surface_validate(&surface_linear16) == ODM_STATUS_INVALID_DATA);
        surface_linear16.alpha_mode = ODM_ALPHA_PREMULTIPLIED;

        /* Core sampler optimization parity. The cached row-specialized path
         * must be exactly equivalent to the scalar semantic path for every
         * canonical source representation, including transparent-outside edge
         * sampling. No tolerance is permitted. */
        {
            const odm_render_surface_frame *surfaces[3] = {
                &surface, &surface_linear8, &surface_linear16
            };
            uint32_t si;
            for (si = 0u; si < 3u; ++si) {
                odm_layered_pixel16 scalar_core[32u * 32u];
                odm_layered_pixel16 cached_core[32u * 32u];
                odm_layered_core_raster_plan raster;
                odm_layered_core_map_entry xmap[32u];
                memset(scalar_core, 0, sizeof(scalar_core));
                memset(cached_core, 0, sizeof(cached_core));
                ODM_TEST_CHECK(context,
                    odm_layered_core_prepare_raster(&c, &p, surfaces[si],
                                                    &raster, xmap, 32u) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context,
                    odm_layered_core_render_rows(&c, &p, surfaces[si],
                                                 0u, 32u, scalar_core) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context,
                    odm_layered_core_render_rows_cached(&c, &p, surfaces[si],
                                                        &raster, xmap, 0u, 32u,
                                                        cached_core) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context,
                    memcmp(scalar_core, cached_core, sizeof(scalar_core)) == 0);
            }
        }

        /* Background is an independent authority. With Core, Field and HUD
         * disabled, changing the Core shape cannot alter a background pixel. */
        c2 = c;
        c2.core.opacity_q31 = 0u;
        c2.field.flags = 0u;
        c2.hud.flags = 0u;
        c2.core.shape = ODM_CORE_SHAPE_SQUARE;
        ODM_TEST_CHECK(context,
                       odm_layered_resolve_frame_plan(&c2, &comp, &dir, 24000, 48000, &p2) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_layered_render_frame(&c2, &p2, &surface, NULL,
                                                ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                scratch, sb, out2, fb, &rf, &rs, &h2) == ODM_STATUS_OK);
        c2.core.shape = ODM_CORE_SHAPE_CIRCLE;
        ODM_TEST_CHECK(context,
                       odm_layered_resolve_frame_plan(&c2, &comp, &dir, 24000, 48000, &p2) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_layered_render_frame(&c2, &p2, &surface, NULL,
                                                ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                scratch, sb, out1, fb, &rf, &rs, &h1) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(out1, out2, (size_t)fb) == 0 &&
                                odm_sha256_equal(&h1, &h2));

        /* Reactive Field deliberately follows the exact Core contour. A
         * square and a circle therefore must not collapse to the same field. */
        c2 = c;
        c2.core.opacity_q31 = 0u;
        c2.hud.flags = 0u;
        c2.core.shape = ODM_CORE_SHAPE_SQUARE;
        ODM_TEST_CHECK(context, odm_layered_resolve_frame_plan(&c2, &comp, &dir, 24000, 48000, &p2) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_render_frame(&c2, &p2, &surface, NULL,
                       ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE, scratch, sb, out2, fb, &rf, &rs, &h2) == ODM_STATUS_OK);
        c2.core.shape = ODM_CORE_SHAPE_CIRCLE;
        ODM_TEST_CHECK(context, odm_layered_resolve_frame_plan(&c2, &comp, &dir, 24000, 48000, &p2) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_layered_render_frame(&c2, &p2, &surface, NULL,
                       ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE, scratch, sb, out1, fb, &rf, &rs, &h1) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(out1, out2, (size_t)fb) != 0 && !odm_sha256_equal(&h1, &h2));

        /* Exact contour equations on principal axes.  These are Q16.16
         * geometric identities, not visual tolerances. */
        c2 = c;
        c2.core.shape = ODM_CORE_SHAPE_CIRCLE;
        ODM_TEST_CHECK(context,
                       odm_layered_resolve_frame_plan(&c2, &comp, &dir, 24000, 48000, &p2) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_layered_core_sdf_q16(&c2, &p2, p2.core_radius_q16, 0) == 0);
        ODM_TEST_CHECK(context,
                       odm_layered_core_sdf_q16(&c2, &p2, 0, -p2.core_radius_q16) == 0);
        c2.core.shape = ODM_CORE_SHAPE_SQUARE;
        ODM_TEST_CHECK(context,
                       odm_layered_resolve_frame_plan(&c2, &comp, &dir, 24000, 48000, &p2) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
                       odm_layered_core_sdf_q16(&c2, &p2, p2.core_half_w_q16, 0) == 0);
        ODM_TEST_CHECK(context,
                       odm_layered_core_sdf_q16(&c2, &p2, p2.core_half_w_q16,
                                                p2.core_half_h_q16) == 0);

        /* Particle motion is sample-time authoritative, not export-FPS
         * authoritative. The same exact sample must rasterize the same particle
         * field at 24, 30 and 60 fps. */
        {
            odm_layered_config cfps = c;
            odm_layered_frame_plan pp24, pp60;
            cfps.background.style = ODM_BACKGROUND_NONE;
            cfps.core.opacity_q31 = 0u;
            cfps.field.flags = ODM_FIELD_PARTICLES;
            cfps.hud.flags = 0u;
            cfps.canvas.fps = 24;
            ODM_TEST_CHECK(context,
                           odm_layered_resolve_frame_plan(&cfps, &comp, &dir, 24000, 48000, &pp24) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           odm_layered_render_frame(&cfps, &pp24, &surface, NULL,
                                                    ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                    scratch, sb, out1, fb, &rf, &rs, &h1) == ODM_STATUS_OK);
            cfps.canvas.fps = 60;
            ODM_TEST_CHECK(context,
                           odm_layered_resolve_frame_plan(&cfps, &comp, &dir, 24000, 48000, &pp60) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           odm_layered_render_frame(&cfps, &pp60, &surface, NULL,
                                                    ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                    scratch, sb, out2, fb, &rf, &rs, &h2) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, memcmp(out1, out2, (size_t)fb) == 0 &&
                                    odm_sha256_equal(&h1, &h2));
        }

        /* Particle phase arithmetic remains exact at the int64 sample horizon.
         * Samples separated by 480*2^32 advance every particle by an exact
         * multiple of a full uint32 turn.  The two fields must therefore be
         * byte-identical even where a naive sample*speed product would overflow
         * uint64_t before division. */
        {
            odm_layered_config cext = c;
            odm_layered_frame_plan pa, pb;
            odm_layered_pixel16 fa[32u * 32u], fb16[32u * 32u];
            const int64_t period_samples = (int64_t)(UINT64_C(480) << 32);
            cext.background.style = ODM_BACKGROUND_NONE;
            cext.core.opacity_q31 = 0u;
            cext.hud.flags = 0u;
            cext.field.flags = ODM_FIELD_PARTICLES;
            ODM_TEST_CHECK(context,
                           odm_layered_resolve_frame_plan(&cext, &comp, &dir, 24000, 48000, &pa) == ODM_STATUS_OK);
            pb = pa;
            pa.sample = INT64_MAX - INT64_C(4096);
            pb.sample = pa.sample - period_samples;
            memset(fa, 0, sizeof(fa));
            memset(fb16, 0, sizeof(fb16));
            odm_layered_field_render(&cext, &pa, fa);
            odm_layered_field_render(&cext, &pb, fb16);
            ODM_TEST_CHECK(context, memcmp(fa, fb16, sizeof(fa)) == 0);
        }

        /* With equal radial energy and no particles, circle field geometry is
         * centrally symmetric. This catches directional CORDIC/line raster
         * drift that would otherwise show up as visibly uneven bars. */
        {
            odm_layered_config cs = c;
            odm_composition_frame_state sym = comp;
            uint64_t px;
            cs.background.style = ODM_BACKGROUND_NONE;
            cs.core.opacity_q31 = 0u;
            cs.hud.flags = 0u;
            cs.field.flags = ODM_FIELD_RADIAL_BARS | ODM_FIELD_ORBIT_RING;
            cs.field.primary_color = cs.field.secondary_color;
            sym.ring_phase = 0u;
            for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS; ++i)
                sym.radial_q31[i] = q31_ratio(1u, 2u);
            ODM_TEST_CHECK(context,
                           odm_layered_resolve_frame_plan(&cs, &sym, &dir, 24000, 48000, &p2) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           odm_layered_render_frame(&cs, &p2, &surface, NULL,
                                                    ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                    scratch, sb, out1, fb, &rf, &rs, &h1) == ODM_STATUS_OK);
            for (px = 0u; px < UINT64_C(32) * 32u; ++px) {
                uint64_t x = px % 32u, y = px / 32u;
                uint64_t q = (UINT64_C(31) - y) * 32u + (UINT64_C(31) - x);
                if (memcmp(out1 + px * 4u, out1 + q * 4u, 4u) != 0) break;
            }
            ODM_TEST_CHECK(context, px == UINT64_C(32) * 32u);
        }

        /* Perspective Grid is its own exact background geometry.  The optimized
         * row raster must equal the scalar semantic pixel function for every
         * pixel, and with zero phase/warp it is mirror-symmetric around both
         * canvas axes. */
        {
            odm_layered_config cp = c;
            odm_layered_frame_plan pp;
            odm_layered_pixel16 pr[32u * 32u];
            odm_layered_pixel16 ps[32u * 32u];
            uint64_t k;
            uint64_t lit = 0u;
            cp.background.style = ODM_BACKGROUND_PERSPECTIVE_GRID;
            cp.background.warp_reactivity_q31 = 0u;
            cp.core.opacity_q31 = 0u;
            cp.field.flags = 0u;
            cp.hud.flags = 0u;
            ODM_TEST_CHECK(context,
                           odm_layered_resolve_frame_plan(&cp, &comp, &dir,
                                                          24000, 48000, &pp) == ODM_STATUS_OK);
            pp.grid_offset_x_q16 = 0;
            pp.grid_offset_y_q16 = 0;
            memset(pr, 0, sizeof(pr));
            memset(ps, 0, sizeof(ps));
            odm_layered_background_render_rows(&cp, &pp, 0u, pp.height, pr);
            for (k = 0u; k < UINT64_C(32) * 32u; ++k) {
                uint32_t xx = (uint32_t)(k % 32u);
                uint32_t yy = (uint32_t)(k / 32u);
                ODM_TEST_CHECK(context,
                               odm_layered_background_pixel(&cp, &pp, xx, yy,
                                                            &ps[k]) == ODM_STATUS_OK);
                if (pr[k].r != 0u || pr[k].g != 0u || pr[k].b != 0u) ++lit;
            }
            ODM_TEST_CHECK(context, memcmp(pr, ps, sizeof(pr)) == 0);
            ODM_TEST_CHECK(context, lit > 0u && lit < UINT64_C(32) * 32u);
            for (k = 0u; k < UINT64_C(32) * 32u; ++k) {
                uint64_t xx = k % 32u, yy = k / 32u;
                uint64_t qx = yy * 32u + (UINT64_C(31) - xx);
                uint64_t qy = (UINT64_C(31) - yy) * 32u + xx;
                if (memcmp(&pr[k], &pr[qx], sizeof(pr[k])) != 0 ||
                    memcmp(&pr[k], &pr[qy], sizeof(pr[k])) != 0) break;
            }
            ODM_TEST_CHECK(context, k == UINT64_C(32) * 32u);
        }

        /* HUD geometry is independently selectable.  A full centered capsule
         * progress track is exactly mirror-symmetric, while changing metadata
         * anchor/time/progress style changes only HUD/config identity. */
        {
            odm_layered_config chud = c;
            odm_layered_frame_plan phud;
            uint64_t px;
            chud.background.style = ODM_BACKGROUND_NONE;
            chud.core.opacity_q31 = 0u;
            chud.field.flags = 0u;
            chud.hud.flags = ODM_HUD_PROGRESS_BAR;
            chud.hud.progress_style = ODM_HUD_PROGRESS_CAPSULE;
            chud.hud.metadata_anchor = ODM_HUD_ANCHOR_BOTTOM_CENTER;
            chud.hud.time_mode = ODM_HUD_TIME_REMAINING;
            ODM_TEST_CHECK(context, odm_layered_config_validate(&chud) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           odm_layered_resolve_frame_plan(&chud, &comp, &dir, 48000, 48000, &phud) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           odm_layered_render_frame(&chud, &phud, &surface, NULL,
                                                    ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                    scratch, sb, out1, fb, &rf, &rs, &h1) == ODM_STATUS_OK);
            for (px = 0u; px < UINT64_C(32) * 32u; ++px) {
                uint64_t x = px % 32u, y = px / 32u;
                uint64_t q = y * 32u + (UINT64_C(31) - x);
                if (memcmp(out1 + px * 4u, out1 + q * 4u, 4u) != 0) break;
            }
            ODM_TEST_CHECK(context, px == UINT64_C(32) * 32u);
            chud.hud.progress_style = ODM_HUD_PROGRESS_HAIRLINE;
            chud.hud.metadata_anchor = ODM_HUD_ANCHOR_TOP_RIGHT;
            chud.hud.time_mode = ODM_HUD_TIME_ELAPSED;
            ODM_TEST_CHECK(context, odm_layered_config_validate(&chud) == ODM_STATUS_OK);
            chud.hud.metadata_anchor = ODM_HUD_ANCHOR_BOTTOM_RIGHT + 1u;
            ODM_TEST_CHECK(context, odm_layered_config_validate(&chud) == ODM_STATUS_INVALID_DATA);
            chud.hud.metadata_anchor = ODM_HUD_ANCHOR_TOP_LEFT;
            chud.hud.progress_style = ODM_HUD_PROGRESS_HAIRLINE + 1u;
            ODM_TEST_CHECK(context, odm_layered_config_validate(&chud) == ODM_STATUS_INVALID_DATA);
            chud.hud.progress_style = ODM_HUD_PROGRESS_RECT;
            chud.hud.time_mode = ODM_HUD_TIME_REMAINING + 1u;
            ODM_TEST_CHECK(context, odm_layered_config_validate(&chud) == ODM_STATUS_INVALID_DATA);
        }

        /* Cancellation and undersized buffers are fail-closed: no partial frame. */
        {
            odm_job job;
            odm_job_ticket ticket;
            memset(out1, 0x5a, (size_t)fb);
            ODM_TEST_CHECK(context, odm_job_init(&job) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_job_begin(&job, 1u, &ticket) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_job_request_cancel(&job) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                           odm_layered_render_frame(&c, &p, &surface, &ticket,
                                                    ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                    scratch, sb, out1, fb, &rf, &rs, &h1) == ODM_STATUS_CANCELLED);
            for (i = 0u; i < (uint32_t)fb; ++i) if (out1[i] != 0x5au) break;
            ODM_TEST_CHECK(context, i == (uint32_t)fb);
            ODM_TEST_CHECK(context, odm_job_finish(ticket, ODM_STATUS_CANCELLED) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_job_destroy(&job) == ODM_STATUS_OK);
        }
        memset(out1, 0x5a, (size_t)fb);
        ODM_TEST_CHECK(context,
                       odm_layered_render_frame(&c, &p, &surface, NULL,
                                                ODM_LAYERED_PIXEL_RGBA8_SRGB_BLACK_COMPOSITE,
                                                scratch, sb, out1, fb - 1u, &rf, &rs, &h1) == ODM_STATUS_BUFFER_TOO_SMALL);
        for (i = 0u; i < (uint32_t)fb; ++i) if (out1[i] != 0x5au) break;
        ODM_TEST_CHECK(context, i == (uint32_t)fb);
    }

    ODM_TEST_CHECK(context,
                   odm_export_plan_build(&c, 48001, 48001u,
                                         ODM_LAYERED_PIXEL_RGBA16LE_LINEAR_PREMUL, &ep) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, ep.samples_per_frame == 1600 && ep.frame_count == 31u &&
                            ep.output_end_sample == 49600);
    ODM_TEST_CHECK(context, ep.audio_pad_frames == 1599u);
    ODM_TEST_CHECK(context, ep.video_bytes_per_frame == UINT64_C(32) * 32u * 8u);

    {
        uint8_t cfg_bytes[ODM_LAYERED_CONFIG_BYTES];
        uint64_t cfg_req = 0u;
        ODM_TEST_CHECK(context, odm_layered_config_bytes(&c, cfg_bytes, sizeof(cfg_bytes), &cfg_req) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, cfg_req == ODM_LAYERED_CONFIG_BYTES);
        ODM_TEST_CHECK(context, odm_layered_config_sha256(&c, &h2) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_sha256(cfg_bytes, sizeof(cfg_bytes), &h1) == ODM_STATUS_OK && odm_sha256_equal(&h1, &h2));
        ODM_TEST_CHECK(context, odm_sha256_equal(&ep.config_sha256, &h2));
    }

    ODM_TEST_CHECK(context,
                   odm_layered_policy_bytes(policy_bytes, sizeof(policy_bytes),
                                            &policy_required) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, policy_required == ODM_LAYERED_POLICY_BYTES);
    ODM_TEST_CHECK(context, odm_layered_policy_current_sha256(&policy) == ODM_STATUS_OK);
    ODM_TEST_CHECK(context, odm_sha256(policy_bytes, sizeof(policy_bytes), &h1) == ODM_STATUS_OK &&
                            odm_sha256_equal(&policy, &h1));

    /* PARIDAD DE LAS DOS RUTAS DE FONDO.
     *
     * El render de produccion solo usa la ruta con cache de columnas. La ruta
     * general es la referencia. Si divergen, lo que se exporta no es lo que la
     * referencia describe -- y eso ya paso: DEPTH_FIELD no estaba enumerado en
     * la ruta con cache y se exportaba como un plano de color liso.
     *
     * Este bucle recorre TODOS los estilos, incluidos los que aun no existen
     * cuando se escribe: el limite es ODM_BACKGROUND_STYLE_MAX, asi que anadir
     * un estilo sin darle soporte en ambas rutas rompe el test solo. */
    {
        uint32_t style;
        const uint32_t pw = 64u, ph = 64u;
        odm_layered_pixel16 *ruta_ref = (odm_layered_pixel16 *)malloc(sizeof(*ruta_ref) * pw * ph);
        odm_layered_pixel16 *ruta_cache = (odm_layered_pixel16 *)malloc(sizeof(*ruta_cache) * pw * ph);
        int32_t *shifts = (int32_t *)malloc(sizeof(*shifts) * pw);
        ODM_TEST_CHECK(context, ruta_ref != NULL && ruta_cache != NULL && shifts != NULL);
        if (ruta_ref != NULL && ruta_cache != NULL && shifts != NULL) {
            for (style = 0u; style <= ODM_BACKGROUND_STYLE_MAX; ++style) {
                odm_layered_config bc;
                odm_layered_frame_plan bp;
                odm_composition_frame_state bcomp;
                odm_director_frame_state bdir;
                memset(&bcomp, 0, sizeof(bcomp));
                memset(&bdir, 0, sizeof(bdir));
                bcomp.schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
                bcomp.radial_gain_q31 = (uint32_t)INT32_MAX;
                bcomp.radial_aperture_q31 = (uint32_t)INT32_MAX;
                bcomp.flags = ODM_COMPOSITION_FLAG_STRICT_CAUSAL;
                bdir.schema_version = ODM_DIRECTOR_SCHEMA_VERSION;
                bdir.layout = ODM_DIRECTOR_LAYOUT_MONOLITH;
                ODM_TEST_CHECK(context,
                    odm_layered_config_init_default(&bc, ODM_CANVAS_ASPECT_SQUARE_1_1,
                                                    pw, ph, 30) == ODM_STATUS_OK);
                bc.background.style = style;
                /* Un fondo no negro: con negro sobre negro, dos rutas rotas
                 * coinciden y el test pasaria sin demostrar nada. */
                bc.background.solid_color.r = 8000u;
                bc.background.solid_color.g = 10000u;
                bc.background.solid_color.b = 14000u;
                bc.background.solid_color.a = UINT16_MAX;
                bc.background.grid_color.r = 50000u;
                bc.background.grid_color.g = 55000u;
                bc.background.grid_color.b = 60000u;
                bc.background.grid_color.a = UINT16_MAX;
                ODM_TEST_CHECK(context, odm_layered_config_validate(&bc) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context,
                    odm_layered_resolve_frame_plan(&bc, &bcomp, &bdir, 0, 48000, &bp)
                        == ODM_STATUS_OK);
                memset(ruta_ref, 0xA5, sizeof(*ruta_ref) * pw * ph);
                memset(ruta_cache, 0x5A, sizeof(*ruta_cache) * pw * ph);
                odm_layered_background_render_rows(&bc, &bp, 0u, ph, ruta_ref);
                odm_layered_background_prepare_columns(&bp, shifts, pw);
                odm_layered_background_render_rows_cached(&bc, &bp, shifts, 0u, ph, ruta_cache);
                ODM_TEST_CHECK(context, memcmp(ruta_ref, ruta_cache, sizeof(*ruta_ref) * pw * ph) == 0);

                /* Y ademas tiene que dibujar algo: un estilo que rellena un
                 * plano liso pasaria la paridad y seguiria estando roto. Solo
                 * NONE y SOLID son legitimamente uniformes. */
                if (style != ODM_BACKGROUND_NONE && style != ODM_BACKGROUND_SOLID) {
                    uint32_t k, distintos = 0u;
                    for (k = 1u; k < pw * ph; ++k) {
                        if (ruta_ref[k].r != ruta_ref[0].r || ruta_ref[k].g != ruta_ref[0].g ||
                            ruta_ref[k].b != ruta_ref[0].b) { distintos = 1u; break; }
                    }
                    ODM_TEST_CHECK(context, distintos == 1u);
                }
            }
        }
        free(ruta_ref); free(ruta_cache); free(shifts);
    }

    free(scratch); free(out1); free(out2);
    test_composicion_no_toca_la_musica(context);
    test_banda_del_hud_reservada(context);
}
