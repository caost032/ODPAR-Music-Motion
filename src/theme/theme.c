/* Temas con legibilidad demostrada. Contrato en odm_theme.h.
 *
 * La parte que importa de este archivo no son los colores: es que un tema que
 * no se lee NO SE APLICA. La comprobacion de contraste no es un aviso, es una
 * puerta.
 */

#include "odm_theme.h"

#include "odm_renderer.h"
#include "renderer_internal.h"
#include "odm_wire.h"

#include <string.h>

#define TH_Q ((uint32_t)INT32_MAX)

static uint32_t th_q_ratio(uint32_t n, uint32_t d) {
    if (d == 0u || n > d) return 0u;
    return (uint32_t)(((uint64_t)TH_Q * n + d / 2u) / d);
}

/* --- Espacio de autor -> espacio de render --------------------------------- */

static uint16_t th_srgb16_to_linear16(uint16_t v) {
    /* Se interpola la tabla EOTF congelada de 256 entradas en Q4.27 en vez de
     * cuantizar a 8 bits primero. Cuantizar perderia los tonos intermedios que
     * distinguen dos colores de tema vecinos, y con ellos la razon de ser de
     * tener 16 bits de autor. La interpolacion es entera, monotona y exacta. */
    uint32_t p = (uint32_t)(((uint64_t)v * UINT64_C(255) * UINT64_C(256) +
                             UINT64_C(32767)) / UINT64_C(65535));
    uint32_t idx = p >> 8, frac = p & 255u;
    int64_t lo, hi, q27;
    if (idx >= 255u)
        return odm_renderer_q27_to_u16(odm_renderer_decode_u8(255u, ODM_COLOR_TRANSFER_SRGB));
    lo = (int64_t)odm_renderer_decode_u8((uint8_t)idx, ODM_COLOR_TRANSFER_SRGB);
    hi = (int64_t)odm_renderer_decode_u8((uint8_t)(idx + 1u), ODM_COLOR_TRANSFER_SRGB);
    q27 = lo + ((hi - lo) * (int64_t)frac + 128) / 256;
    return odm_renderer_q27_to_u16((odm_render_q4_27)q27);
}

void odm_theme_srgb16_to_linear16(const odm_rgba16 *in_srgb, odm_rgba16 *out_linear) {
    if (!in_srgb || !out_linear) return;
    out_linear->r = th_srgb16_to_linear16(in_srgb->r);
    out_linear->g = th_srgb16_to_linear16(in_srgb->g);
    out_linear->b = th_srgb16_to_linear16(in_srgb->b);
    out_linear->a = in_srgb->a;
}

/* --- Contraste ----------------------------------------------------------- */

uint32_t odm_theme_relative_luminance(const odm_rgba16 *color) {
    /* Los canales del tema se declaran como codigos sRGB de 16 bits. Se llevan
     * a luz lineal con la EOTF congelada del motor -- la misma que usa el
     * compositor -- porque la luminancia relativa solo tiene sentido en lineal.
     * Promediar o comparar codigos sRGB directamente daria contrastes falsos. */
    uint32_t r8, g8, b8;
    int64_t lr, lg, lb, y;
    if (!color) return 0u;
    r8 = (uint32_t)(((uint32_t)color->r * 255u + 32767u) / 65535u);
    g8 = (uint32_t)(((uint32_t)color->g * 255u + 32767u) / 65535u);
    b8 = (uint32_t)(((uint32_t)color->b * 255u + 32767u) / 65535u);
    lr = (int64_t)odm_renderer_decode_u8((uint8_t)r8, ODM_COLOR_TRANSFER_SRGB);
    lg = (int64_t)odm_renderer_decode_u8((uint8_t)g8, ODM_COLOR_TRANSFER_SRGB);
    lb = (int64_t)odm_renderer_decode_u8((uint8_t)b8, ODM_COLOR_TRANSFER_SRGB);
    /* Coeficientes BT.709 en diezmilesimas: 0.2126 / 0.7152 / 0.0722. */
    y = (lr * 2126 + lg * 7152 + lb * 722) / 10000;
    if (y < 0) y = 0;
    /* Q4.27 -> Q1.31 sin perder rango. */
    y = (y * (int64_t)TH_Q) / (int64_t)ODM_RENDER_Q27_ONE;
    if (y > (int64_t)TH_Q) y = (int64_t)TH_Q;
    return (uint32_t)y;
}

uint32_t odm_theme_contrast_ratio(const odm_rgba16 *a, const odm_rgba16 *b) {
    /* (L_claro + 0.05) / (L_oscuro + 0.05), en centesimas.
     * El 0.05 de WCAG modela la reflexion ambiental de una pantalla real: sin
     * el, negro contra negro daria una division por cero en lugar de 1:1. */
    uint64_t la, lb, hi, lo, offset;
    if (!a || !b) return 0u;
    la = odm_theme_relative_luminance(a);
    lb = odm_theme_relative_luminance(b);
    hi = la > lb ? la : lb;
    lo = la > lb ? lb : la;
    offset = (uint64_t)TH_Q / 20u;   /* 0.05 en Q1.31 */
    hi += offset;
    lo += offset;
    if (lo == 0u) return 0u;
    return (uint32_t)((hi * 100u + lo / 2u) / lo);
}

odm_status odm_theme_validate(const odm_theme *theme,
                              odm_theme_contrast_report *out_report) {
    odm_theme_contrast_report r;
    const odm_rgba16 *bg;
    if (!theme || !out_report) return ODM_STATUS_INVALID_ARGUMENT;
    if (theme->schema_version != ODM_THEME_SCHEMA_VERSION) return ODM_STATUS_VERSION_MISMATCH;

    memset(&r, 0, sizeof(r));
    r.schema_version = ODM_THEME_SCHEMA_VERSION;
    r.failing_role = (uint32_t)ODM_THEME_ROLE_COUNT;

    /* El fondo de referencia es el plano dominante, no el mas profundo: es
     * sobre el que caen el texto y el halo en la mayor parte del cuadro. */
    bg = &theme->role[ODM_THEME_ROLE_BACKGROUND_BASE];

    r.title_on_background    = odm_theme_contrast_ratio(&theme->role[ODM_THEME_ROLE_TEXT_PRIMARY], bg);
    r.metadata_on_background = odm_theme_contrast_ratio(&theme->role[ODM_THEME_ROLE_TEXT_SECONDARY], bg);
    r.accent_on_background   = odm_theme_contrast_ratio(&theme->role[ODM_THEME_ROLE_ACCENT], bg);
    r.primary_on_background  = odm_theme_contrast_ratio(&theme->role[ODM_THEME_ROLE_PRIMARY], bg);
    r.secondary_on_background= odm_theme_contrast_ratio(&theme->role[ODM_THEME_ROLE_SECONDARY], bg);

    r.passed = 1u;
    /* El titulo es texto y se exige el minimo de texto. Los metadatos suelen
     * ir a tamano grande en el HUD, asi que se les aplica el umbral de texto
     * grande. El resto son elementos graficos. */
    if (r.title_on_background < ODM_THEME_CONTRAST_TEXT_MIN) {
        r.passed = 0u; r.failing_role = ODM_THEME_ROLE_TEXT_PRIMARY;
    } else if (r.metadata_on_background < ODM_THEME_CONTRAST_LARGE_MIN) {
        r.passed = 0u; r.failing_role = ODM_THEME_ROLE_TEXT_SECONDARY;
    } else if (r.accent_on_background < ODM_THEME_CONTRAST_GRAPHIC_MIN) {
        r.passed = 0u; r.failing_role = ODM_THEME_ROLE_ACCENT;
    } else if (r.primary_on_background < ODM_THEME_CONTRAST_GRAPHIC_MIN) {
        r.passed = 0u; r.failing_role = ODM_THEME_ROLE_PRIMARY;
    }

    *out_report = r;
    return r.passed ? ODM_STATUS_OK : ODM_STATUS_INVALID_DATA;
}

/* --- Temas integrados ----------------------------------------------------
 *
 * Cada uno es una decision completa, no una variacion de color. Cambian fondo,
 * gramatica del halo, densidad, tipografia y paleta a la vez, y todos pasan la
 * puerta de contraste por construccion: se comprueba en los tests, no se
 * confia en el ojo de quien los escribio. */

static odm_rgba16 rgb(uint16_t r, uint16_t g, uint16_t b) {
    odm_rgba16 c; c.r = r; c.g = g; c.b = b; c.a = UINT16_MAX; return c;
}

typedef struct { const char *name; void (*fill)(odm_theme *t); } th_entry;

static void th_common(odm_theme *t) {
    memset(t, 0, sizeof(*t));
    t->schema_version = ODM_THEME_SCHEMA_VERSION;
    t->core_shape = ODM_THEME_CORE_CIRCLE;
    t->core_size_q31 = th_q_ratio(38u, 100u);
    t->core_border_q31 = th_q_ratio(1u, 720u);
    t->field = ODM_THEME_FIELD_FILAMENT;
    t->field_length_q31 = th_q_ratio(1u, 7u);
    t->field_weight_q31 = th_q_ratio(1u, 150u);
    t->particle_density_q31 = 0u;
    t->hud_show_progress = 1u;
    t->hud_show_time = 1u;
    t->hud_show_title = 0u;
    t->hud_show_artist = 0u;
    t->hud_progress_width_q31 = th_q_ratio(9u, 10u);
    t->hud_text_scale_q31 = th_q_ratio(1u, 1u);
    /* El fondo es fondo: sugiere un espacio, no compite con el nucleo. */
    t->background_intensity_q31 = th_q_ratio(1u, 8u);
}

/* VOID: negro absoluto, un solo acento calido. El halo es lo unico vivo. */
static void th_void(odm_theme *t) {
    th_common(t);
    t->background = ODM_THEME_BG_DEPTH_FIELD;
    t->role[ODM_THEME_ROLE_BACKGROUND_DEEP] = rgb(0, 0, 0);
    t->role[ODM_THEME_ROLE_BACKGROUND_BASE] = rgb(0, 0, 0);
    t->role[ODM_THEME_ROLE_SURFACE]         = rgb(9000, 9500, 11000);
    t->role[ODM_THEME_ROLE_PRIMARY]         = rgb(30000, 32000, 36000);
    t->role[ODM_THEME_ROLE_SECONDARY]       = rgb(18000, 19000, 22000);
    t->role[ODM_THEME_ROLE_ACCENT]          = rgb(65535, 62000, 48000);
    t->role[ODM_THEME_ROLE_TEXT_PRIMARY]    = rgb(60000, 60000, 62000);
    t->role[ODM_THEME_ROLE_TEXT_SECONDARY]  = rgb(40000, 41000, 44000);
    t->role[ODM_THEME_ROLE_EVENT_HIGHLIGHT] = rgb(65535, 65535, 65535);
}

/* ARCHITECT: rejilla en fuga, azul frio, estructura visible. */
static void th_architect(odm_theme *t) {
    th_common(t);
    t->background = ODM_THEME_BG_PERSPECTIVE;
    t->background_intensity_q31 = th_q_ratio(2u, 5u);
    t->field = ODM_THEME_FIELD_BARS;
    t->field_length_q31 = th_q_ratio(1u, 9u);
    t->field_weight_q31 = th_q_ratio(1u, 120u);
    t->core_shape = ODM_THEME_CORE_ROUNDED;
    t->role[ODM_THEME_ROLE_BACKGROUND_DEEP] = rgb(0, 1200, 2600);
    t->role[ODM_THEME_ROLE_BACKGROUND_BASE] = rgb(1000, 2200, 4200);
    t->role[ODM_THEME_ROLE_SURFACE]         = rgb(9000, 14000, 22000);
    t->role[ODM_THEME_ROLE_PRIMARY]         = rgb(26000, 40000, 56000);
    t->role[ODM_THEME_ROLE_SECONDARY]       = rgb(14000, 22000, 34000);
    t->role[ODM_THEME_ROLE_ACCENT]          = rgb(40000, 62000, 65535);
    t->role[ODM_THEME_ROLE_TEXT_PRIMARY]    = rgb(58000, 62000, 65535);
    t->role[ODM_THEME_ROLE_TEXT_SECONDARY]  = rgb(36000, 44000, 54000);
    t->role[ODM_THEME_ROLE_EVENT_HIGHLIGHT] = rgb(65535, 65535, 65535);
}

/* PAPER: fondo claro. Existe para forzar que nada del motor asuma fondo
 * oscuro; si algo se rompe con luz, se ve aqui. */
static void th_paper(odm_theme *t) {
    th_common(t);
    t->background = ODM_THEME_BG_SOLID;
    t->field = ODM_THEME_FIELD_FILAMENT;
    t->role[ODM_THEME_ROLE_BACKGROUND_DEEP] = rgb(61000, 61000, 59000);
    t->role[ODM_THEME_ROLE_BACKGROUND_BASE] = rgb(63500, 63500, 62000);
    t->role[ODM_THEME_ROLE_SURFACE]         = rgb(52000, 52000, 50000);
    t->role[ODM_THEME_ROLE_PRIMARY]         = rgb(14000, 14000, 15000);
    t->role[ODM_THEME_ROLE_SECONDARY]       = rgb(30000, 30000, 31000);
    t->role[ODM_THEME_ROLE_ACCENT]          = rgb(20000, 6000, 2000);
    t->role[ODM_THEME_ROLE_TEXT_PRIMARY]    = rgb(4000, 4000, 5000);
    t->role[ODM_THEME_ROLE_TEXT_SECONDARY]  = rgb(16000, 16000, 18000);
    t->role[ODM_THEME_ROLE_EVENT_HIGHLIGHT] = rgb(0, 0, 0);
}

/* EMBER: brasa. Corona corta y densa, sin rejilla. */
static void th_ember(odm_theme *t) {
    th_common(t);
    t->background = ODM_THEME_BG_DEPTH_FIELD;
    t->field = ODM_THEME_FIELD_CORONA;
    t->field_length_q31 = th_q_ratio(1u, 11u);
    t->field_weight_q31 = th_q_ratio(1u, 110u);
    t->particle_density_q31 = th_q_ratio(1u, 6u);
    t->role[ODM_THEME_ROLE_BACKGROUND_DEEP] = rgb(1800, 400, 200);
    t->role[ODM_THEME_ROLE_BACKGROUND_BASE] = rgb(3000, 800, 400);
    t->role[ODM_THEME_ROLE_SURFACE]         = rgb(14000, 5000, 2000);
    t->role[ODM_THEME_ROLE_PRIMARY]         = rgb(44000, 20000, 8000);
    t->role[ODM_THEME_ROLE_SECONDARY]       = rgb(22000, 9000, 4000);
    t->role[ODM_THEME_ROLE_ACCENT]          = rgb(65535, 40000, 12000);
    t->role[ODM_THEME_ROLE_TEXT_PRIMARY]    = rgb(64000, 58000, 50000);
    t->role[ODM_THEME_ROLE_TEXT_SECONDARY]  = rgb(44000, 36000, 30000);
    t->role[ODM_THEME_ROLE_EVENT_HIGHLIGHT] = rgb(65535, 60000, 40000);
}

/* MONO: sin color. La jerarquia la lleva enteramente la luminancia. */
static void th_mono(odm_theme *t) {
    th_common(t);
    t->background = ODM_THEME_BG_VOID;
    t->field = ODM_THEME_FIELD_FILAMENT;
    t->field_length_q31 = th_q_ratio(1u, 6u);
    t->core_shape = ODM_THEME_CORE_SQUARE;
    t->hud_progress_width_q31 = TH_Q;
    t->role[ODM_THEME_ROLE_BACKGROUND_DEEP] = rgb(0, 0, 0);
    t->role[ODM_THEME_ROLE_BACKGROUND_BASE] = rgb(0, 0, 0);
    t->role[ODM_THEME_ROLE_SURFACE]         = rgb(11000, 11000, 11000);
    t->role[ODM_THEME_ROLE_PRIMARY]         = rgb(34000, 34000, 34000);
    t->role[ODM_THEME_ROLE_SECONDARY]       = rgb(19000, 19000, 19000);
    t->role[ODM_THEME_ROLE_ACCENT]          = rgb(65535, 65535, 65535);
    t->role[ODM_THEME_ROLE_TEXT_PRIMARY]    = rgb(58000, 58000, 58000);
    t->role[ODM_THEME_ROLE_TEXT_SECONDARY]  = rgb(38000, 38000, 38000);
    t->role[ODM_THEME_ROLE_EVENT_HIGHLIGHT] = rgb(65535, 65535, 65535);
}

static const th_entry th_table[] = {
    { "VOID",      th_void },
    { "MONO",      th_mono },
    { "ARCHITECT", th_architect },
    { "EMBER",     th_ember },
    { "PAPER",     th_paper }
};

uint32_t odm_theme_count(void) {
    return (uint32_t)(sizeof(th_table) / sizeof(th_table[0]));
}

const char *odm_theme_builtin_name(uint32_t index) {
    if (index >= odm_theme_count()) return NULL;
    return th_table[index].name;
}

odm_status odm_theme_builtin(uint32_t index, odm_theme *out_theme) {
    size_t n;
    if (!out_theme || index >= odm_theme_count()) return ODM_STATUS_INVALID_ARGUMENT;
    th_table[index].fill(out_theme);
    n = strlen(th_table[index].name);
    if (n >= ODM_THEME_NAME_BYTES) return ODM_STATUS_INVARIANT_BROKEN;
    memcpy(out_theme->name, th_table[index].name, n + 1u);
    return ODM_STATUS_OK;
}

/* --- Materializacion ----------------------------------------------------- */

/* Fraccion del lado menor -> Q16.16 en pixeles. Todas las medidas se derivan de
 * aqui: un tema pensado a 540 tiene que verse igual de intencionado a 2160. */
static uint32_t th_px(uint32_t dim, uint32_t q31) {
    return (uint32_t)((((uint64_t)dim << 16) * (uint64_t)q31) / (uint64_t)TH_Q);
}

odm_status odm_theme_apply(const odm_theme *theme,
                           uint32_t aspect, uint32_t width, uint32_t height,
                           int32_t fps, uint64_t seed,
                           odm_layered_config *out_config) {
    odm_layered_config c;
    odm_theme_contrast_report report;
    odm_status st;
    uint32_t dim;

    if (!theme || !out_config) return ODM_STATUS_INVALID_ARGUMENT;

    /* Puerta de legibilidad. Materializar un tema que no pasa seria producir un
     * video en el que no se lee el titulo, que es exactamente el fallo que este
     * subsistema existe para hacer imposible. */
    st = odm_theme_validate(theme, &report);
    if (st != ODM_STATUS_OK) return st;

    st = odm_layered_config_init_default(&c, aspect, width, height, fps);
    if (st != ODM_STATUS_OK) return st;
    dim = width < height ? width : height;

    /* CATEGORIA FONDO ---------------------------------------------------- */
    switch (theme->background) {
        case ODM_THEME_BG_VOID:        c.background.style = ODM_BACKGROUND_NONE; break;
        case ODM_THEME_BG_DEPTH_FIELD: c.background.style = ODM_BACKGROUND_DEPTH_FIELD; break;
        case ODM_THEME_BG_PERSPECTIVE: c.background.style = ODM_BACKGROUND_PERSPECTIVE_GRID; break;
        case ODM_THEME_BG_FLAT_GRID:   c.background.style = ODM_BACKGROUND_GRID; break;
        case ODM_THEME_BG_SOLID:       c.background.style = ODM_BACKGROUND_SOLID; break;
        default: return ODM_STATUS_INVALID_DATA;
    }
    c.background.solid_color = theme->role[ODM_THEME_ROLE_BACKGROUND_BASE];
    c.background.grid_color = theme->role[ODM_THEME_ROLE_SURFACE];
    c.background.grid_spacing_q16 = th_px(dim, th_q_ratio(1u, 5u));
    c.background.grid_line_q16 = th_px(dim, th_q_ratio(1u, 900u));
    if (c.background.grid_line_q16 == 0u) c.background.grid_line_q16 = 1u << 12;
    c.background.grid_feather_q16 = 1u << 16;
    c.background.opacity_q31 = theme->background_intensity_q31;

    /* CATEGORIA NUCLEO --------------------------------------------------- */
    switch (theme->core_shape) {
        case ODM_THEME_CORE_CIRCLE:  c.core.shape = ODM_CORE_SHAPE_CIRCLE; c.core.corner_radius_q31 = 0u; break;
        case ODM_THEME_CORE_ROUNDED: c.core.shape = ODM_CORE_SHAPE_ROUNDED_RECT; c.core.corner_radius_q31 = th_q_ratio(1u, 6u); break;
        case ODM_THEME_CORE_SQUARE:  c.core.shape = ODM_CORE_SHAPE_ROUNDED_RECT; c.core.corner_radius_q31 = 0u; break;
        default: return ODM_STATUS_INVALID_DATA;
    }
    c.core.fit = ODM_CORE_FIT_COVER;
    c.core.width_q31 = theme->core_size_q31;
    c.core.height_q31 = theme->core_size_q31;
    c.core.border_q16 = th_px(dim, theme->core_border_q31);
    if (c.core.border_q16 == 0u) c.core.border_q16 = 1u << 12;
    c.core.feather_q16 = th_px(dim, th_q_ratio(1u, 900u));
    if (c.core.feather_q16 == 0u) c.core.feather_q16 = 1u << 12;
    c.core.border_color = theme->role[ODM_THEME_ROLE_SURFACE];
    c.core.scale_reactivity_q31 = th_q_ratio(1u, 5u);

    /* CATEGORIA CAMPO ---------------------------------------------------- */
    c.field.flags = 0u;
    c.field.radial_segments = ODM_COMPOSITION_RADIAL_SEGMENTS_MAX;
    if (theme->field != ODM_THEME_FIELD_NONE) c.field.flags |= ODM_FIELD_RADIAL_BARS;
    c.field.bar_max_q16 = th_px(dim, theme->field_length_q31);
    c.field.bar_min_q16 = 0u;
    c.field.bar_width_q16 = th_px(dim, theme->field_weight_q31);
    if (c.field.bar_width_q16 == 0u) c.field.bar_width_q16 = 1u << 12;
    c.field.ring_gap_q16 = th_px(dim, th_q_ratio(1u, 44u));
    c.field.field_opacity_q31 = th_q_ratio(8u, 10u);
    c.field.primary_color = theme->role[ODM_THEME_ROLE_ACCENT];
    c.field.secondary_color = theme->role[ODM_THEME_ROLE_PRIMARY];
    c.field.seed = seed;

    /* CATEGORIA PARTICULAS ----------------------------------------------- */
    if (theme->particle_density_q31 != 0u) {
        uint64_t n = ((uint64_t)theme->particle_density_q31 * 96u) / TH_Q;
        c.field.particle_count = (uint32_t)(n > 128u ? 128u : n);
        if (c.field.particle_count != 0u) c.field.flags |= ODM_FIELD_PARTICLES;
        c.field.particle_radius_q16 = th_px(dim, th_q_ratio(1u, 700u));
        if (c.field.particle_radius_q16 == 0u) c.field.particle_radius_q16 = 1u << 12;
    } else {
        c.field.particle_count = 0u;
        c.field.particle_radius_q16 = 1u << 16;
    }

    /* CATEGORIA HUD ------------------------------------------------------- */
    c.hud.flags = 0u;
    if (theme->hud_show_progress) c.hud.flags |= ODM_HUD_PROGRESS_BAR;
    if (theme->hud_show_time)     c.hud.flags |= ODM_HUD_TIME_CODE;
    if (theme->hud_show_title)    c.hud.flags |= ODM_HUD_TITLE;
    if (theme->hud_show_artist)   c.hud.flags |= ODM_HUD_ARTIST;
    c.hud.margin_q16 = th_px(dim, th_q_ratio(1u, 18u));
    c.hud.progress_height_q16 = th_px(dim, th_q_ratio(1u, 420u));
    if (c.hud.progress_height_q16 == 0u) c.hud.progress_height_q16 = 1u << 16;
    c.hud.progress_width_q31 = theme->hud_progress_width_q31;
    {
        /* Escala tipografica entera: el rasterizado cae en rejilla y el texto
         * no sale borroso por una escala fraccionaria. */
        uint64_t base = ((uint64_t)dim * (uint64_t)theme->hud_text_scale_q31) / TH_Q;
        uint32_t scale = (uint32_t)(base / 180u);
        if (scale < 2u) scale = 2u;
        if (scale > 24u) scale = 24u;
        c.hud.text_scale_q16 = scale << 16;
    }
    c.hud.line_gap_q16 = th_px(dim, th_q_ratio(1u, 200u));
    c.hud.foreground_color = theme->role[ODM_THEME_ROLE_TEXT_SECONDARY];
    c.hud.background_color.a = 0u;
    c.hud.progress_style = ODM_HUD_PROGRESS_HAIRLINE;
    c.hud.time_mode = ODM_HUD_TIME_ELAPSED;
    c.hud.metadata_anchor = ODM_HUD_ANCHOR_BOTTOM_CENTER;

    st = odm_layered_config_validate(&c);
    if (st != ODM_STATUS_OK) return st;
    *out_config = c;
    return ODM_STATUS_OK;
}

odm_status odm_theme_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                  uint64_t *out_required) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    odm_status st;
    uint32_t i;
    const uint64_t bytes = 512u;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = bytes;
    if (!buffer || capacity < bytes) return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, (size_t)bytes);
    st = odm_wire_writer_init(&w, buffer, bytes);
    if (st != ODM_STATUS_OK) return st;
#define TP(x) do { st = (x); if (st != ODM_STATUS_OK) return st; } while (0)
    TP(odm_wire_write_bytes(&w, "ODMTHEM1", 8u));
    TP(odm_wire_write_u32(&w, ODM_THEME_POLICY_VERSION));
    TP(odm_wire_write_u32(&w, ODM_THEME_SCHEMA_VERSION));
    TP(odm_wire_write_u32(&w, (uint32_t)ODM_THEME_ROLE_COUNT));
    TP(odm_wire_write_u32(&w, (uint32_t)ODM_THEME_BG_COUNT));
    TP(odm_wire_write_u32(&w, (uint32_t)ODM_THEME_FIELD_COUNT));
    TP(odm_wire_write_u32(&w, (uint32_t)ODM_THEME_CORE_COUNT));
    TP(odm_wire_write_u32(&w, odm_theme_count()));
    TP(odm_wire_write_u32(&w, ODM_THEME_CONTRAST_TEXT_MIN));
    TP(odm_wire_write_u32(&w, ODM_THEME_CONTRAST_LARGE_MIN));
    TP(odm_wire_write_u32(&w, ODM_THEME_CONTRAST_GRAPHIC_MIN));
    TP(odm_wire_write_u32(&w, 1u)); /* luminancia relativa en luz lineal BT.709 */
    TP(odm_wire_write_u32(&w, 1u)); /* ratio WCAG con desplazamiento 0.05 */
    TP(odm_wire_write_u32(&w, 1u)); /* un tema que no pasa NO se materializa */
    TP(odm_wire_write_u32(&w, 1u)); /* categorias no se mezclan entre si */
    TP(odm_wire_write_u32(&w, 1u)); /* los colores son de autor en sRGB y se linealizan al compilar */
    for (i = 0u; i < odm_theme_count(); ++i) {
        odm_theme t;
        odm_theme_contrast_report rep;
        if (odm_theme_builtin(i, &t) != ODM_STATUS_OK) return ODM_STATUS_INVARIANT_BROKEN;
        (void)odm_theme_validate(&t, &rep);
        TP(odm_wire_write_u32(&w, i));
        TP(odm_wire_write_u32(&w, t.background));
        TP(odm_wire_write_u32(&w, t.field));
        TP(odm_wire_write_u32(&w, t.core_shape));
        TP(odm_wire_write_u32(&w, rep.title_on_background));
        TP(odm_wire_write_u32(&w, rep.metadata_on_background));
        TP(odm_wire_write_u32(&w, rep.accent_on_background));
    }
#undef TP
    return odm_wire_writer_finish(&w, out_required);
}
