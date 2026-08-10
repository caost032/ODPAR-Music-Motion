/* Plantillas, diseno y esquema de controles. Contrato en odm_design.h.
 *
 * Tres cosas viven aqui y conviene no confundirlas:
 *
 *   - LA TABLA DE CONTROLES es el esquema que el motor publica. Es la unica
 *     descripcion de que se puede editar, en que rango y con que etiquetas.
 *   - LAS PLANTILLAS son puntos de partida. Se compilan a un diseno explicito y
 *     dejan de tener autoridad en ese mismo instante.
 *   - LA COMPILACION traduce un diseno a parametros efectivos de capas. Es la
 *     UNICA ruta de materializacion: los temas tambien pasan por aqui, para que
 *     no existan dos sitios que deban coincidir.
 */

#include "odm_design.h"

#include "odm_wire.h"

#include <stddef.h>
#include <string.h>

#define DZ_Q ((uint32_t)INT32_MAX)

static uint32_t dz_ratio(uint32_t n, uint32_t d) {
    if (d == 0u || n > d) return 0u;
    return (uint32_t)(((uint64_t)DZ_Q * n + d / 2u) / d);
}

static uint32_t dz_mul(uint32_t a_q31, uint32_t b_q31) {
    return (uint32_t)(((uint64_t)a_q31 * (uint64_t)b_q31) / (uint64_t)DZ_Q);
}

/* Espacio de autor -> espacio de render. TODO color que cruce hacia la
 * configuracion de capas pasa por aqui: el compositor mezcla en luz lineal y el
 * diseno se escribe en codigos sRGB. Que la conversion este en un solo punto es
 * lo que impide que un color se cuele sin convertir y salga mas claro de lo
 * escrito -- que es exactamente lo que pasaba con el fondo. */
static odm_rgba16 dz_lin(const odm_rgba16 *autor) {
    odm_rgba16 out;
    odm_theme_srgb16_to_linear16(autor, &out);
    return out;
}

/* Fraccion del lado menor -> pixeles en Q16.16. Todas las medidas salen de
 * aqui: un diseno pensado a 720 debe leerse igual de intencionado a 2160. */
static uint32_t dz_px(uint32_t dim, uint32_t q31) {
    return (uint32_t)((((uint64_t)dim << 16) * (uint64_t)q31) / (uint64_t)DZ_Q);
}

static int dz_copy_ascii(char *dst, uint32_t cap, const char *src) {
    uint32_t n = 0u, i;
    memset(dst, 0, cap);
    if (!src) return 1;
    while (src[n] != '\0') {
        unsigned char ch = (unsigned char)src[n];
        if (n + 1u >= cap || ch < 32u || ch > 126u) return 0;
        ++n;
    }
    for (i = 0u; i < n; ++i) dst[i] = src[i];
    return 1;
}

/* --- Tabla de controles ---------------------------------------------------
 *
 * El desplazamiento es interno a proposito. La app identifica un control por su
 * clave estable ("fondo.estilo"), nunca por su posicion ni por su direccion:
 * asi se pueden reordenar o insertar controles sin romper proyectos guardados.
 */

typedef struct {
    uint32_t id, category, kind, min_value, max_value, default_value, option_count;
    uint16_t offset;
    const char *key;
    const char *label;
    const char *const *options;
} dz_control;

#define DZ_OFF(f) ((uint16_t)offsetof(odm_design, f))

static const char *const dz_opt_aspect[] = {
    "1:1 cuadrado", "16:9 horizontal", "9:16 vertical", "4:5 vertical", "4:3 horizontal"
};
static const char *const dz_opt_background[] = {
    "Vacio", "Plano de color", "Rejilla", "Rejilla en fuga", "Campo de profundidad",
    "Horizonte", "Anillos concentricos", "Matriz de puntos", "Degradado"
};
static const char *const dz_opt_core_shape[] = { "Circulo", "Cuadrado", "Rectangulo redondeado" };
static const char *const dz_opt_core_fit[]   = { "Llenar", "Contener", "Estirar" };
static const char *const dz_opt_field[]      = { "Filamentos", "Barras", "Corona", "Anillo", "Ninguno" };
static const char *const dz_opt_bar[]        = { "Trazo", "Capsula", "Cuna", "Puntos" };
static const char *const dz_opt_sym[]        = { "Ninguna", "Espejo", "Cuadrante" };
static const char *const dz_opt_blur[]       = { "Ninguno", "Suave", "Medio", "Alto", "Maximo" };
static const char *const dz_opt_detail[]     = { "48 sectores", "96 sectores" };
static const char *const dz_opt_anchor[]     = {
    "Arriba izquierda", "Arriba centro", "Arriba derecha",
    "Abajo izquierda", "Abajo centro", "Abajo derecha"
};
static const char *const dz_opt_progress[]   = { "Rectangulo", "Capsula", "Linea fina" };
static const char *const dz_opt_time[]       = { "Transcurrido y total", "Transcurrido", "Restante" };

/* Rangos escalares como fracciones del lado menor, salvo donde se indique. */
#define DZ_SCALAR(id_, cat_, key_, lab_, field_, lo_, hi_, def_) \
    { id_, cat_, ODM_DESIGN_KIND_SCALAR, lo_, hi_, def_, 0u, DZ_OFF(field_), key_, lab_, NULL }
#define DZ_ENUM(id_, cat_, key_, lab_, field_, n_, def_, opts_) \
    { id_, cat_, ODM_DESIGN_KIND_ENUM, 0u, (n_) - 1u, def_, n_, DZ_OFF(field_), key_, lab_, opts_ }
#define DZ_TOGGLE(id_, cat_, key_, lab_, field_, def_) \
    { id_, cat_, ODM_DESIGN_KIND_TOGGLE, 0u, 1u, def_, 0u, DZ_OFF(field_), key_, lab_, NULL }
#define DZ_COLOR(id_, cat_, key_, lab_, field_) \
    { id_, cat_, ODM_DESIGN_KIND_COLOR, 0u, 0u, 0u, 0u, DZ_OFF(field_), key_, lab_, NULL }

/* Fracciones usadas como limites y valores por omision. Se escriben como
 * razones enteras y se convierten a Q1.31 al publicarse, para que el esquema no
 * dependa de constantes magicas escritas a mano. */
#define R(n, d) (((uint32_t)((((uint64_t)0x7fffffffu * (n)) + (d) / 2u) / (d))))

static const dz_control dz_table[] = {
    /* LIENZO */
    DZ_ENUM(1u, ODM_DESIGN_CAT_CANVAS, "lienzo.encuadre", "Encuadre",
            aspect, 5u, 0u, dz_opt_aspect),

    /* FONDO */
    DZ_ENUM(10u, ODM_DESIGN_CAT_BACKGROUND, "fondo.estilo", "Estilo de fondo",
            background.style, 9u, 4u, dz_opt_background),
    DZ_COLOR(11u, ODM_DESIGN_CAT_BACKGROUND, "fondo.color", "Color de fondo",
             background.color),
    DZ_COLOR(12u, ODM_DESIGN_CAT_BACKGROUND, "fondo.acento", "Color de trama",
             background.accent),
    DZ_SCALAR(13u, ODM_DESIGN_CAT_BACKGROUND, "fondo.intensidad", "Intensidad",
              background.intensity_q31, 0u, R(1u,1u), R(1u,2u)),
    DZ_SCALAR(14u, ODM_DESIGN_CAT_BACKGROUND, "fondo.escala", "Escala de la trama",
              background.scale_q31, R(1u,64u), R(1u,3u), R(1u,5u)),
    DZ_SCALAR(15u, ODM_DESIGN_CAT_BACKGROUND, "fondo.grosor", "Grosor de linea",
              background.line_q31, R(1u,4000u), R(1u,120u), R(1u,900u)),
    DZ_SCALAR(16u, ODM_DESIGN_CAT_BACKGROUND, "fondo.reactividad", "Reactividad",
              background.reactivity_q31, 0u, R(1u,1u), R(1u,8u)),

    /* NUCLEO */
    DZ_ENUM(20u, ODM_DESIGN_CAT_CORE, "nucleo.forma", "Forma",
            core.shape, 3u, 0u, dz_opt_core_shape),
    DZ_ENUM(21u, ODM_DESIGN_CAT_CORE, "nucleo.encuadre", "Encuadre del medio",
            core.fit, 3u, 0u, dz_opt_core_fit),
    DZ_SCALAR(22u, ODM_DESIGN_CAT_CORE, "nucleo.tamano", "Tamano",
              core.size_q31, R(1u,10u), R(4u,5u), R(38u,100u)),
    DZ_SCALAR(23u, ODM_DESIGN_CAT_CORE, "nucleo.esquina", "Radio de esquina",
              core.corner_q31, 0u, R(1u,2u), R(1u,6u)),
    DZ_SCALAR(24u, ODM_DESIGN_CAT_CORE, "nucleo.borde", "Ancho del borde",
              core.border_q31, 0u, R(1u,50u), R(1u,720u)),
    DZ_COLOR(25u, ODM_DESIGN_CAT_CORE, "nucleo.borde_color", "Color del borde",
             core.border_color),
    DZ_SCALAR(26u, ODM_DESIGN_CAT_CORE, "nucleo.opacidad", "Opacidad",
              core.opacity_q31, 0u, R(1u,1u), R(1u,1u)),
    DZ_SCALAR(27u, ODM_DESIGN_CAT_CORE, "nucleo.reactividad", "Reactividad de escala",
              core.reactivity_q31, 0u, R(1u,1u), R(2u,5u)),

    /* CAMPO */
    DZ_ENUM(30u, ODM_DESIGN_CAT_FIELD, "campo.gramatica", "Composicion del halo",
            field.grammar, 5u, 0u, dz_opt_field),
    DZ_ENUM(37u, ODM_DESIGN_CAT_FIELD, "campo.forma", "Forma de la barra",
            field.shape, 4u, 1u, dz_opt_bar),
    DZ_SCALAR(31u, ODM_DESIGN_CAT_FIELD, "campo.longitud", "Alcance",
              field.length_q31, R(1u,40u), R(1u,3u), R(1u,7u)),
    DZ_SCALAR(32u, ODM_DESIGN_CAT_FIELD, "campo.grosor", "Grosor del trazo",
              field.weight_q31, R(1u,4000u), R(1u,25u), R(1u,150u)),
    DZ_SCALAR(33u, ODM_DESIGN_CAT_FIELD, "campo.opacidad", "Opacidad",
              field.opacity_q31, 0u, R(1u,1u), R(1u,1u)),
    DZ_ENUM(34u, ODM_DESIGN_CAT_FIELD, "campo.detalle", "Detalle angular",
            field.detail, 2u, 1u, dz_opt_detail),
    DZ_ENUM(38u, ODM_DESIGN_CAT_FIELD, "campo.simetria", "Simetria del arco",
            field.symmetry, 3u, 0u, dz_opt_sym),
    DZ_ENUM(39u, ODM_DESIGN_CAT_FIELD, "campo.difuminado", "Difuminado entre bandas",
            field.band_blur, 5u, 1u, dz_opt_blur),
    DZ_SCALAR(3010u, ODM_DESIGN_CAT_FIELD, "campo.suavizado_subida", "Rapidez de subida",
              field.smooth_rise_q31, R(1u,50u), R(1u,1u), R(4u,5u)),
    DZ_SCALAR(3011u, ODM_DESIGN_CAT_FIELD, "campo.suavizado_bajada", "Rapidez de bajada",
              field.smooth_fall_q31, R(1u,50u), R(1u,1u), R(1u,6u)),
    DZ_COLOR(35u, ODM_DESIGN_CAT_FIELD, "campo.color", "Color del cuerpo",
             field.color),
    DZ_COLOR(36u, ODM_DESIGN_CAT_FIELD, "campo.acento", "Color del ataque",
             field.accent),

    /* PARTICULAS */
    DZ_TOGGLE(40u, ODM_DESIGN_CAT_PARTICLES, "particulas.activas", "Particulas",
              particles.enabled, 0u),
    DZ_SCALAR(41u, ODM_DESIGN_CAT_PARTICLES, "particulas.densidad", "Densidad",
              particles.density_q31, 0u, R(1u,1u), R(1u,6u)),
    DZ_SCALAR(42u, ODM_DESIGN_CAT_PARTICLES, "particulas.tamano", "Tamano",
              particles.size_q31, R(1u,4000u), R(1u,120u), R(1u,700u)),
    DZ_COLOR(43u, ODM_DESIGN_CAT_PARTICLES, "particulas.color", "Color",
             particles.color),

    /* TEXTO */
    DZ_TOGGLE(50u, ODM_DESIGN_CAT_TEXT, "texto.titulo", "Mostrar titulo",
              text.show_title, 0u),
    DZ_TOGGLE(51u, ODM_DESIGN_CAT_TEXT, "texto.autoria", "Mostrar autoria",
              text.show_artist, 0u),
    DZ_COLOR(52u, ODM_DESIGN_CAT_TEXT, "texto.titulo_color", "Color del titulo",
             text.title_color),
    DZ_COLOR(53u, ODM_DESIGN_CAT_TEXT, "texto.autoria_color", "Color de la autoria",
             text.artist_color),
    DZ_SCALAR(54u, ODM_DESIGN_CAT_TEXT, "texto.escala", "Tamano tipografico",
              text.scale_q31, R(1u,4u), R(1u,1u), R(1u,2u)),
    DZ_ENUM(55u, ODM_DESIGN_CAT_TEXT, "texto.anclaje", "Posicion",
            text.anchor, 6u, 4u, dz_opt_anchor),

    /* PROGRESO Y TIEMPO */
    DZ_TOGGLE(60u, ODM_DESIGN_CAT_PROGRESS, "progreso.barra", "Mostrar barra",
              progress.show_bar, 1u),
    DZ_TOGGLE(61u, ODM_DESIGN_CAT_PROGRESS, "progreso.tiempo", "Mostrar tiempo",
              progress.show_time, 1u),
    DZ_ENUM(62u, ODM_DESIGN_CAT_PROGRESS, "progreso.estilo", "Estilo de barra",
            progress.style, 3u, 2u, dz_opt_progress),
    DZ_ENUM(63u, ODM_DESIGN_CAT_PROGRESS, "progreso.modo_tiempo", "Modo de tiempo",
            progress.time_mode, 3u, 1u, dz_opt_time),
    DZ_SCALAR(64u, ODM_DESIGN_CAT_PROGRESS, "progreso.ancho", "Ancho",
              progress.width_q31, R(1u,10u), R(1u,1u), R(9u,10u)),
    DZ_SCALAR(65u, ODM_DESIGN_CAT_PROGRESS, "progreso.alto", "Grosor",
              progress.height_q31, R(1u,4000u), R(1u,60u), R(1u,420u)),
    DZ_COLOR(66u, ODM_DESIGN_CAT_PROGRESS, "progreso.color", "Color de la barra",
             progress.color),
    DZ_COLOR(67u, ODM_DESIGN_CAT_PROGRESS, "progreso.pista_color", "Color de la pista",
             progress.track_color),

    /* MOVIMIENTO */
    DZ_SCALAR(70u, ODM_DESIGN_CAT_MOTION, "movimiento.sensibilidad", "Sensibilidad",
              motion.sensitivity_q31, 0u, R(1u,1u), R(4u,5u)),
    DZ_SCALAR(71u, ODM_DESIGN_CAT_MOTION, "movimiento.ataque", "Peso del ataque",
              motion.attack_q31, 0u, R(1u,1u), R(3u,4u)),
    DZ_SCALAR(72u, ODM_DESIGN_CAT_MOTION, "movimiento.caida", "Peso de la cola",
              motion.release_q31, 0u, R(1u,1u), R(1u,2u))
};

static const uint32_t dz_aspect_value[5] = {
    ODM_CANVAS_ASPECT_SQUARE_1_1, ODM_CANVAS_ASPECT_HORIZONTAL_16_9,
    ODM_CANVAS_ASPECT_VERTICAL_9_16, ODM_CANVAS_ASPECT_VERTICAL_4_5,
    ODM_CANVAS_ASPECT_HORIZONTAL_4_3
};

uint32_t odm_design_control_count(void) {
    return (uint32_t)(sizeof(dz_table) / sizeof(dz_table[0]));
}

static const dz_control *dz_by_id(uint32_t id) {
    uint32_t i, n = odm_design_control_count();
    for (i = 0u; i < n; ++i) if (dz_table[i].id == id) return &dz_table[i];
    return NULL;
}

static odm_status dz_publish(const dz_control *c, odm_design_control *out) {
    memset(out, 0, sizeof(*out));
    out->id = c->id; out->category = c->category; out->kind = c->kind;
    out->min_value = c->min_value; out->max_value = c->max_value;
    out->default_value = c->default_value; out->option_count = c->option_count;
    if (!dz_copy_ascii(out->key, ODM_DESIGN_KEY_BYTES, c->key)) return ODM_STATUS_INVARIANT_BROKEN;
    if (!dz_copy_ascii(out->label, ODM_DESIGN_LABEL_BYTES, c->label)) return ODM_STATUS_INVARIANT_BROKEN;
    return ODM_STATUS_OK;
}

odm_status odm_design_control_at(uint32_t index, odm_design_control *out_control) {
    if (!out_control || index >= odm_design_control_count()) return ODM_STATUS_INVALID_ARGUMENT;
    return dz_publish(&dz_table[index], out_control);
}

odm_status odm_design_control_find(const char *key, odm_design_control *out_control) {
    uint32_t i, n = odm_design_control_count();
    if (!key || !out_control) return ODM_STATUS_INVALID_ARGUMENT;
    for (i = 0u; i < n; ++i) {
        if (strcmp(dz_table[i].key, key) == 0) return dz_publish(&dz_table[i], out_control);
    }
    return ODM_STATUS_INVALID_ARGUMENT;
}

const char *odm_design_option_label(uint32_t control_id, uint32_t option) {
    const dz_control *c = dz_by_id(control_id);
    if (!c || c->kind != ODM_DESIGN_KIND_ENUM || !c->options ||
        option >= c->option_count) return NULL;
    return c->options[option];
}

const char *odm_design_category_name(uint32_t category) {
    static const char *const names[ODM_DESIGN_CAT_COUNT] = {
        "Encuadre", "Fondo", "Imagen central", "Composicion",
        "Particulas", "Texto", "Progreso", "Movimiento"
    };
    if (category >= ODM_DESIGN_CAT_COUNT) return NULL;
    return names[category];
}

/* --- Lectura y escritura -------------------------------------------------- */

odm_status odm_design_get(const odm_design *design, uint32_t control_id,
                          uint32_t *out_value) {
    const dz_control *c;
    if (!design || !out_value) return ODM_STATUS_INVALID_ARGUMENT;
    c = dz_by_id(control_id);
    if (!c) return ODM_STATUS_INVALID_ARGUMENT;
    if (c->kind == ODM_DESIGN_KIND_COLOR) return ODM_STATUS_INVALID_ARGUMENT;
    memcpy(out_value, (const uint8_t *)design + c->offset, sizeof(*out_value));
    return ODM_STATUS_OK;
}

odm_status odm_design_set(odm_design *design, uint32_t control_id, uint32_t value) {
    const dz_control *c;
    if (!design) return ODM_STATUS_INVALID_ARGUMENT;
    c = dz_by_id(control_id);
    if (!c) return ODM_STATUS_INVALID_ARGUMENT;
    if (c->kind == ODM_DESIGN_KIND_COLOR) return ODM_STATUS_INVALID_ARGUMENT;
    /* Fuera de rango es un fallo, no un recorte. Recortar en silencio dejaria a
     * la app mostrando un valor que el motor no tiene. */
    if (value < c->min_value || value > c->max_value) return ODM_STATUS_INVALID_DATA;
    memcpy((uint8_t *)design + c->offset, &value, sizeof(value));
    return ODM_STATUS_OK;
}

odm_status odm_design_get_color(const odm_design *design, uint32_t control_id,
                                odm_rgba16 *out_color) {
    const dz_control *c;
    if (!design || !out_color) return ODM_STATUS_INVALID_ARGUMENT;
    c = dz_by_id(control_id);
    if (!c) return ODM_STATUS_INVALID_ARGUMENT;
    if (c->kind != ODM_DESIGN_KIND_COLOR) return ODM_STATUS_INVALID_ARGUMENT;
    memcpy(out_color, (const uint8_t *)design + c->offset, sizeof(*out_color));
    return ODM_STATUS_OK;
}

odm_status odm_design_set_color(odm_design *design, uint32_t control_id,
                                const odm_rgba16 *color) {
    const dz_control *c;
    if (!design || !color) return ODM_STATUS_INVALID_ARGUMENT;
    c = dz_by_id(control_id);
    if (!c) return ODM_STATUS_INVALID_ARGUMENT;
    if (c->kind != ODM_DESIGN_KIND_COLOR) return ODM_STATUS_INVALID_ARGUMENT;
    memcpy((uint8_t *)design + c->offset, color, sizeof(*color));
    return ODM_STATUS_OK;
}

odm_status odm_design_set_metadata(odm_design *design, const char *title,
                                   const char *artist) {
    if (!design) return ODM_STATUS_INVALID_ARGUMENT;
    if (!dz_copy_ascii(design->text.title, ODM_LAYERED_METADATA_BYTES, title))
        return ODM_STATUS_INVALID_ARGUMENT;
    if (!dz_copy_ascii(design->text.artist, ODM_LAYERED_METADATA_BYTES, artist))
        return ODM_STATUS_INVALID_ARGUMENT;
    return ODM_STATUS_OK;
}

/* --- Diseno base ---------------------------------------------------------- */

/* Todos los valores por omision salen de la tabla de controles. Si salieran de
 * otro sitio existirian dos verdades sobre "lo normal" y acabarian divergiendo:
 * es literalmente el fallo que este motor acaba de corregir en el fondo. */
static void dz_defaults(odm_design *d, uint32_t aspect) {
    uint32_t i, n = odm_design_control_count();
    memset(d, 0, sizeof(*d));
    d->schema_version = ODM_DESIGN_SCHEMA_VERSION;
    for (i = 0u; i < n; ++i) {
        const dz_control *c = &dz_table[i];
        if (c->kind == ODM_DESIGN_KIND_COLOR) continue;
        memcpy((uint8_t *)d + c->offset, &c->default_value, sizeof(c->default_value));
    }
    d->aspect = aspect;
}

/* --- De tema a diseno ----------------------------------------------------- */

odm_status odm_design_from_theme(const odm_theme *theme, uint32_t aspect,
                                 odm_design *out_design) {
    odm_design d;
    odm_theme_contrast_report rep;
    odm_status st;
    if (!theme || !out_design) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_theme_validate(theme, &rep);
    if (st != ODM_STATUS_OK) return st;

    dz_defaults(&d, aspect);
    d.theme_id = UINT32_MAX;
    memcpy(d.name, theme->name, sizeof(d.name) < sizeof(theme->name)
                                ? sizeof(d.name) : sizeof(theme->name));
    d.name[ODM_DESIGN_NAME_BYTES - 1u] = '\0';

    switch (theme->background) {
        case ODM_THEME_BG_VOID:        d.background.style = ODM_BACKGROUND_NONE; break;
        case ODM_THEME_BG_DEPTH_FIELD: d.background.style = ODM_BACKGROUND_DEPTH_FIELD; break;
        case ODM_THEME_BG_PERSPECTIVE: d.background.style = ODM_BACKGROUND_PERSPECTIVE_GRID; break;
        case ODM_THEME_BG_FLAT_GRID:   d.background.style = ODM_BACKGROUND_GRID; break;
        case ODM_THEME_BG_SOLID:       d.background.style = ODM_BACKGROUND_SOLID; break;
        default: return ODM_STATUS_INVALID_DATA;
    }
    d.background.color = theme->role[ODM_THEME_ROLE_BACKGROUND_BASE];
    d.background.accent = theme->role[ODM_THEME_ROLE_SURFACE];
    d.background.intensity_q31 = theme->background_intensity_q31;

    switch (theme->core_shape) {
        case ODM_THEME_CORE_CIRCLE:
            d.core.shape = 0u; d.core.corner_q31 = 0u; break;
        case ODM_THEME_CORE_ROUNDED:
            d.core.shape = 2u; d.core.corner_q31 = dz_ratio(1u, 6u); break;
        case ODM_THEME_CORE_SQUARE:
            d.core.shape = 2u; d.core.corner_q31 = 0u; break;
        default: return ODM_STATUS_INVALID_DATA;
    }
    d.core.size_q31 = theme->core_size_q31;
    d.core.border_q31 = theme->core_border_q31;
    d.core.border_color = theme->role[ODM_THEME_ROLE_SURFACE];

    switch (theme->field) {
        case ODM_THEME_FIELD_FILAMENT: d.field.grammar = ODM_DESIGN_FIELD_FILAMENT; break;
        case ODM_THEME_FIELD_BARS:     d.field.grammar = ODM_DESIGN_FIELD_BARS; break;
        case ODM_THEME_FIELD_CORONA:   d.field.grammar = ODM_DESIGN_FIELD_CORONA; break;
        case ODM_THEME_FIELD_NONE:     d.field.grammar = ODM_DESIGN_FIELD_NONE; break;
        default: return ODM_STATUS_INVALID_DATA;
    }
    d.field.length_q31 = theme->field_length_q31;
    d.field.weight_q31 = theme->field_weight_q31;
    d.field.shape = ODM_FIELD_BAR_CAPSULE;
    d.field.color = theme->role[ODM_THEME_ROLE_PRIMARY];
    d.field.accent = theme->role[ODM_THEME_ROLE_ACCENT];

    d.particles.enabled = theme->particle_density_q31 != 0u ? 1u : 0u;
    d.particles.density_q31 = theme->particle_density_q31;
    d.particles.color = theme->role[ODM_THEME_ROLE_EVENT_HIGHLIGHT];

    d.text.show_title = theme->hud_show_title ? 1u : 0u;
    d.text.show_artist = theme->hud_show_artist ? 1u : 0u;
    d.text.title_color = theme->role[ODM_THEME_ROLE_TEXT_PRIMARY];
    d.text.artist_color = theme->role[ODM_THEME_ROLE_TEXT_SECONDARY];
    if (theme->hud_text_scale_q31 != 0u) d.text.scale_q31 = theme->hud_text_scale_q31 / 2u;

    d.progress.show_bar = theme->hud_show_progress ? 1u : 0u;
    d.progress.show_time = theme->hud_show_time ? 1u : 0u;
    d.progress.width_q31 = theme->hud_progress_width_q31;
    d.progress.color = theme->role[ODM_THEME_ROLE_TEXT_SECONDARY];
    /* La pista sale del mismo rol que el texto, no de la superficie: la
     * superficie esta pensada para leerse SOBRE el fondo profundo, y a baja
     * opacidad desaparecia. Una barra cuyo recorrido no se ve no comunica
     * cuanto queda. */
    d.progress.track_color = theme->role[ODM_THEME_ROLE_TEXT_SECONDARY];
    d.progress.track_color.a = UINT16_C(14000);

    *out_design = d;
    return ODM_STATUS_OK;
}

/* --- Plantillas ------------------------------------------------------------
 *
 * Una plantilla decide de golpe lo que un usuario no quiere decidir campo a
 * campo la primera vez: tema, fondo, composicion del halo, forma del nucleo y
 * que informacion se muestra. Todo lo que fija sigue siendo editable despues.
 */

typedef struct {
    const char *name;
    const char *summary;
    uint32_t theme_index;
    uint32_t background;
    uint32_t grammar;
    uint32_t core_shape;
    uint32_t particles;
    uint32_t show_text;
    uint32_t progress_style;
} dz_template;

static const dz_template dz_templates[] = {
    { "Estudio", "Campo de profundidad, filamentos y linea fina. El punto de partida neutro.",
      0u, ODM_BACKGROUND_DEPTH_FIELD, ODM_DESIGN_FIELD_FILAMENT, 0u, 0u, 1u,
      ODM_HUD_PROGRESS_HAIRLINE },
    { "Concierto", "Corona densa sobre brasa, con particulas y barra en capsula.",
      3u, ODM_BACKGROUND_DEPTH_FIELD, ODM_DESIGN_FIELD_CORONA, 0u, 1u, 1u,
      ODM_HUD_PROGRESS_CAPSULE },
    { "Arquitectura", "Rejilla en fuga y barras separadas. Estructura visible.",
      2u, ODM_BACKGROUND_PERSPECTIVE_GRID, ODM_DESIGN_FIELD_BARS, 2u, 0u, 1u,
      ODM_HUD_PROGRESS_RECT },
    { "Minimo", "Sin fondo y sin halo. Solo el nucleo y el tiempo.",
      1u, ODM_BACKGROUND_NONE, ODM_DESIGN_FIELD_NONE, 2u, 0u, 0u,
      ODM_HUD_PROGRESS_HAIRLINE },
    { "Editorial", "Fondo claro, anillos concentricos y tipografia presente.",
      4u, ODM_BACKGROUND_CONCENTRIC, ODM_DESIGN_FIELD_FILAMENT, 0u, 0u, 1u,
      ODM_HUD_PROGRESS_RECT },
    { "Horizonte", "Horizonte y anillo orbital. Composicion con suelo.",
      0u, ODM_BACKGROUND_HORIZON, ODM_DESIGN_FIELD_RING, 0u, 1u, 1u,
      ODM_HUD_PROGRESS_HAIRLINE },
    { "Trama", "Matriz de puntos y barras. Textura sin peso.",
      1u, ODM_BACKGROUND_DOT_MATRIX, ODM_DESIGN_FIELD_BARS, 1u, 0u, 1u,
      ODM_HUD_PROGRESS_HAIRLINE },
    { "Degradado", "Degradado difuminado y filamentos largos.",
      2u, ODM_BACKGROUND_GRADIENT, ODM_DESIGN_FIELD_FILAMENT, 0u, 1u, 1u,
      ODM_HUD_PROGRESS_CAPSULE }
};

uint32_t odm_template_count(void) {
    return (uint32_t)(sizeof(dz_templates) / sizeof(dz_templates[0]));
}

const char *odm_template_name(uint32_t index) {
    if (index >= odm_template_count()) return NULL;
    return dz_templates[index].name;
}

const char *odm_template_summary(uint32_t index) {
    if (index >= odm_template_count()) return NULL;
    return dz_templates[index].summary;
}

odm_status odm_template_load(uint32_t index, uint32_t aspect, odm_design *out_design) {
    const dz_template *t;
    odm_theme theme;
    odm_design d;
    odm_status st;
    if (!out_design || index >= odm_template_count()) return ODM_STATUS_INVALID_ARGUMENT;
    t = &dz_templates[index];
    st = odm_theme_builtin(t->theme_index, &theme);
    if (st != ODM_STATUS_OK) return st;
    st = odm_design_from_theme(&theme, aspect, &d);
    if (st != ODM_STATUS_OK) return st;

    d.template_id = index;
    d.theme_id = t->theme_index;
    if (!dz_copy_ascii(d.name, ODM_DESIGN_NAME_BYTES, t->name))
        return ODM_STATUS_INVARIANT_BROKEN;

    d.background.style = t->background;
    d.field.grammar = t->grammar;
    d.core.shape = t->core_shape;
    if (t->core_shape == 2u && d.core.corner_q31 == 0u) d.core.corner_q31 = dz_ratio(1u, 8u);
    if (t->core_shape != 2u) d.core.corner_q31 = 0u;
    d.particles.enabled = t->particles;
    if (t->particles && d.particles.density_q31 == 0u) d.particles.density_q31 = dz_ratio(1u, 5u);
    d.text.show_title = t->show_text;
    d.text.show_artist = t->show_text;
    d.progress.style = t->progress_style;

    /* Una plantilla que produce un diseno ilegible no es una plantilla, es un
     * fallo que llegaria al usuario como un video sin titulo visible. */
    {
        odm_design_report rep;
        st = odm_design_validate(&d, &rep);
        if (st != ODM_STATUS_OK) return st;
    }
    *out_design = d;
    return ODM_STATUS_OK;
}

/* --- Validacion ----------------------------------------------------------- */

odm_status odm_design_validate(const odm_design *design, odm_design_report *out_report) {
    odm_design_report r;
    uint32_t i, n = odm_design_control_count();
    if (!design || !out_report) return ODM_STATUS_INVALID_ARGUMENT;
    if (design->schema_version != ODM_DESIGN_SCHEMA_VERSION) return ODM_STATUS_VERSION_MISMATCH;

    memset(&r, 0, sizeof(r));
    r.schema_version = ODM_DESIGN_SCHEMA_VERSION;
    r.failing_category = ODM_DESIGN_CAT_COUNT;
    r.passed = 1u;

    /* Rango de todos los controles no cromaticos, leidos por la misma tabla que
     * los publica: no puede existir un control cuyo rango declarado y rango
     * comprobado difieran. */
    for (i = 0u; i < n; ++i) {
        const dz_control *c = &dz_table[i];
        uint32_t v;
        if (c->kind == ODM_DESIGN_KIND_COLOR) continue;
        memcpy(&v, (const uint8_t *)design + c->offset, sizeof(v));
        if (v < c->min_value || v > c->max_value) {
            r.passed = 0u; r.failing_category = c->category;
            *out_report = r;
            return ODM_STATUS_INVALID_DATA;
        }
    }

    /* Puerta de contraste sobre el documento YA EDITADO. Se comprueba lo que se
     * va a dibujar: si el titulo esta oculto, su color no puede hacer fallar
     * nada, y si esta visible tiene que leerse. */
    r.title_contrast  = odm_theme_contrast_ratio(&design->text.title_color, &design->background.color);
    r.artist_contrast = odm_theme_contrast_ratio(&design->text.artist_color, &design->background.color);
    r.field_contrast  = odm_theme_contrast_ratio(&design->field.accent, &design->background.color);

    if (design->text.show_title && r.title_contrast < ODM_THEME_CONTRAST_TEXT_MIN) {
        r.passed = 0u; r.failing_category = ODM_DESIGN_CAT_TEXT;
    } else if (design->text.show_artist && r.artist_contrast < ODM_THEME_CONTRAST_LARGE_MIN) {
        r.passed = 0u; r.failing_category = ODM_DESIGN_CAT_TEXT;
    } else if (design->field.grammar != ODM_DESIGN_FIELD_NONE &&
               r.field_contrast < ODM_THEME_CONTRAST_GRAPHIC_MIN) {
        r.passed = 0u; r.failing_category = ODM_DESIGN_CAT_FIELD;
    } else if (design->progress.show_bar) {
        uint32_t pc = odm_theme_contrast_ratio(&design->progress.color, &design->background.color);
        if (pc < ODM_THEME_CONTRAST_GRAPHIC_MIN) {
            r.passed = 0u; r.failing_category = ODM_DESIGN_CAT_PROGRESS;
        }
    }

    *out_report = r;
    return r.passed ? ODM_STATUS_OK : ODM_STATUS_INVALID_DATA;
}

/* --- Compilacion ----------------------------------------------------------- */

/* Caracter de cada gramatica. La gramatica decide la FORMA; los controles de
 * alcance y grosor la matizan. Si la gramatica no hiciera mas que fijar esos dos
 * numeros, seria un control redundante disfrazado. */
typedef struct {
    uint32_t bars, ring;
    uint32_t len_num, len_den;
    uint32_t weight_num, weight_den;
    uint32_t gap_den;
} dz_grammar;

static const dz_grammar dz_grammars[ODM_DESIGN_FIELD_COUNT] = {
    { 1u, 0u, 1u, 1u, 1u, 1u, 44u },  /* FILAMENTO */
    { 1u, 0u, 3u, 4u, 3u, 2u, 30u },  /* BARRAS     */
    { 1u, 0u, 1u, 2u, 2u, 1u, 60u },  /* CORONA     */
    { 1u, 1u, 2u, 3u, 5u, 4u, 24u },  /* ANILLO     */
    { 0u, 0u, 1u, 1u, 1u, 1u, 44u }   /* NINGUNO    */
};

static uint32_t dz_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

odm_status odm_design_compile(const odm_design *design,
                              uint32_t width, uint32_t height,
                              int32_t fps, uint64_t seed,
                              odm_layered_config *out_config) {
    odm_layered_config c;
    odm_design_report rep;
    odm_status st;
    uint32_t dim, aspect;
    const dz_grammar *g;

    if (!design || !out_config) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_design_validate(design, &rep);
    if (st != ODM_STATUS_OK) return st;
    if (design->aspect >= 5u) return ODM_STATUS_INVALID_DATA;
    aspect = dz_aspect_value[design->aspect];

    st = odm_layered_config_init_default(&c, aspect, width, height, fps);
    if (st != ODM_STATUS_OK) return st;
    /* Un diseno se entrega en 8 bits, asi que se difumina al codificar. Sin
     * esto, cualquier degradado oscuro sale escalonado en anillos. */
    c.flags |= ODM_LAYERED_FLAG_DITHER_OUTPUT;
    dim = width < height ? width : height;

    /* FONDO -------------------------------------------------------------- */
    c.background.style = design->background.style;
    c.background.solid_color = dz_lin(&design->background.color);
    c.background.grid_color = dz_lin(&design->background.accent);
    c.background.opacity_q31 = design->background.intensity_q31;
    /* Los limites de rejilla son legalidad de raster, no estetica: una
     * separacion por debajo de cuatro pixeles no se puede dibujar. El recorte
     * depende solo del tamano de lienzo, que ya forma parte de la
     * configuracion, asi que sigue siendo determinista y reproducible. */
    c.background.grid_spacing_q16 =
        dz_clamp_u32(dz_px(dim, design->background.scale_q31), 4u << 16, 4096u << 16);
    c.background.grid_line_q16 =
        dz_clamp_u32(dz_px(dim, design->background.line_q31), 1u << 12,
                     c.background.grid_spacing_q16 / 2u);
    c.background.grid_feather_q16 = 1u << 16;
    c.background.zoom_reactivity_q31 =
        dz_mul(design->background.reactivity_q31, design->motion.sensitivity_q31);
    c.background.warp_reactivity_q31 = c.background.zoom_reactivity_q31 / 2u;
    c.background.depth_reactivity_q31 = c.background.zoom_reactivity_q31 / 2u;

    /* NUCLEO ------------------------------------------------------------- */
    switch (design->core.shape) {
        case 0u: c.core.shape = ODM_CORE_SHAPE_CIRCLE; c.core.corner_radius_q31 = 0u; break;
        case 1u: c.core.shape = ODM_CORE_SHAPE_ROUNDED_RECT; c.core.corner_radius_q31 = 0u; break;
        case 2u: c.core.shape = ODM_CORE_SHAPE_ROUNDED_RECT;
                 c.core.corner_radius_q31 = design->core.corner_q31 != 0u
                                            ? design->core.corner_q31 : dz_ratio(1u, 8u);
                 break;
        default: return ODM_STATUS_INVALID_DATA;
    }
    c.core.fit = design->core.fit + 1u;   /* 0..2 -> COVER/CONTAIN/STRETCH */
    c.core.width_q31 = design->core.size_q31;
    c.core.height_q31 = design->core.size_q31;
    c.core.border_q16 = dz_px(dim, design->core.border_q31);
    if (c.core.border_q16 == 0u) c.core.border_q16 = 1u << 12;
    c.core.feather_q16 = dz_px(dim, dz_ratio(1u, 900u));
    if (c.core.feather_q16 == 0u) c.core.feather_q16 = 1u << 12;
    c.core.border_color = dz_lin(&design->core.border_color);
    c.core.opacity_q31 = design->core.opacity_q31;
    c.core.scale_reactivity_q31 =
        dz_mul(design->core.reactivity_q31, design->motion.sensitivity_q31);

    /* CAMPO -------------------------------------------------------------- */
    if (design->field.grammar >= ODM_DESIGN_FIELD_COUNT) return ODM_STATUS_INVALID_DATA;
    g = &dz_grammars[design->field.grammar];
    c.field.flags = 0u;
    if (g->bars) c.field.flags |= ODM_FIELD_RADIAL_BARS;
    if (g->ring) c.field.flags |= ODM_FIELD_ORBIT_RING;
    c.field.radial_segments = design->field.detail != 0u
                              ? ODM_COMPOSITION_RADIAL_SEGMENTS_MAX
                              : ODM_COMPOSITION_RADIAL_SEGMENTS;
    c.field.bar_min_q16 = 0u;
    c.field.bar_max_q16 = dz_px(dim, (uint32_t)(((uint64_t)design->field.length_q31 *
                                                 g->len_num) / g->len_den));
    c.field.bar_width_q16 = dz_px(dim, (uint32_t)(((uint64_t)design->field.weight_q31 *
                                                   g->weight_num) / g->weight_den));
    if (c.field.bar_width_q16 == 0u) c.field.bar_width_q16 = 1u << 12;
    c.field.ring_gap_q16 = dz_px(dim, dz_ratio(1u, g->gap_den));
    c.field.field_opacity_q31 = design->field.opacity_q31;
    c.field.primary_color = dz_lin(&design->field.accent);
    c.field.secondary_color = dz_lin(&design->field.color);
    c.field.bar_shape = design->field.shape;
    c.field.seed = seed;

    /* PARTICULAS --------------------------------------------------------- */
    if (design->particles.enabled && design->particles.density_q31 != 0u) {
        uint64_t count = ((uint64_t)design->particles.density_q31 * 192u) / DZ_Q;
        c.field.particle_count = (uint32_t)(count > 192u ? 192u : count);
        if (c.field.particle_count != 0u) c.field.flags |= ODM_FIELD_PARTICLES;
        c.field.particle_radius_q16 = dz_px(dim, design->particles.size_q31);
        if (c.field.particle_radius_q16 == 0u) c.field.particle_radius_q16 = 1u << 12;
    } else {
        c.field.particle_count = 0u;
        c.field.particle_radius_q16 = 1u << 16;
    }
    c.field.particle_color = dz_lin(&design->particles.color);

    /* TEXTO Y PROGRESO --------------------------------------------------- */
    c.hud.flags = 0u;
    if (design->progress.show_bar)  c.hud.flags |= ODM_HUD_PROGRESS_BAR;
    if (design->progress.show_time) c.hud.flags |= ODM_HUD_TIME_CODE;
    if (design->text.show_title && design->text.title[0] != '\0')
        c.hud.flags |= ODM_HUD_TITLE;
    if (design->text.show_artist && design->text.artist[0] != '\0')
        c.hud.flags |= ODM_HUD_ARTIST;
    memcpy(c.hud.title, design->text.title, sizeof(c.hud.title));
    memcpy(c.hud.artist, design->text.artist, sizeof(c.hud.artist));
    c.hud.margin_q16 = dz_px(dim, dz_ratio(1u, 18u));
    c.hud.progress_height_q16 = dz_px(dim, design->progress.height_q31);
    if (c.hud.progress_height_q16 == 0u) c.hud.progress_height_q16 = 1u << 16;
    c.hud.progress_width_q31 = design->progress.width_q31;
    c.hud.progress_style = design->progress.style;
    c.hud.time_mode = design->progress.time_mode;
    c.hud.metadata_anchor = design->text.anchor;
    {
        /* Escala tipografica entera: el rasterizado cae en rejilla y el texto no
         * sale borroso por una escala fraccionaria. */
        uint64_t base = ((uint64_t)dim * (uint64_t)design->text.scale_q31) / DZ_Q;
        uint32_t scale = (uint32_t)(base / 90u);
        c.hud.text_scale_q16 = dz_clamp_u32(scale, 2u, 24u) << 16;
    }
    c.hud.line_gap_q16 = dz_px(dim, dz_ratio(1u, 200u));
    c.hud.foreground_color = dz_lin(&design->progress.color);
    c.hud.title_color = dz_lin(&design->text.title_color);
    c.hud.artist_color = dz_lin(&design->text.artist_color);
    c.hud.progress_color = dz_lin(&design->progress.color);
    c.hud.progress_track_color = dz_lin(&design->progress.track_color);
    c.hud.background_color.r = 0u; c.hud.background_color.g = 0u;
    c.hud.background_color.b = 0u; c.hud.background_color.a = 0u;

    /* RUTAS DE REACCION ---------------------------------------------------
     * Que evidencia musical gobierna que objetivo visual. Se fijan aqui, de
     * forma explicita, y no en quien llame: un diseno compilado tiene que salir
     * ya reactivo. Si cada consumidor tuviera que cablearlas, habria tantas
     * politicas de reaccion como programas, y ninguna seria la del motor. */
    {
        static const uint32_t rutas[6][2] = {
            { ODM_REACTION_TARGET_BACKGROUND_ZOOM,  ODM_REACTION_SOURCE_GRID },
            { ODM_REACTION_TARGET_BACKGROUND_WARP,  ODM_REACTION_SOURCE_FRACTURE },
            { ODM_REACTION_TARGET_BACKGROUND_DEPTH, ODM_REACTION_SOURCE_GRID },
            { ODM_REACTION_TARGET_CORE_SCALE,       ODM_REACTION_SOURCE_CORE_BREATH },
            { ODM_REACTION_TARGET_FIELD_GAIN,       ODM_REACTION_SOURCE_RADIAL_GAIN },
            { ODM_REACTION_TARGET_PARTICLE_DENSITY, ODM_REACTION_SOURCE_PARTICLES }
        };
        uint32_t k;
        for (k = 0u; k < 6u; ++k) {
            uint32_t route = 0u;
            st = odm_reaction_route_make(rutas[k][1], ODM_REACTION_SOURCE_NONE,
                                         ODM_REACTION_COMBINE_A_ONLY, &route);
            if (st != ODM_STATUS_OK) return st;
            st = odm_layered_config_set_reaction_route(&c, rutas[k][0], route);
            if (st != ODM_STATUS_OK) return st;
        }
    }

    st = odm_layered_config_validate(&c);
    if (st != ODM_STATUS_OK) return st;
    *out_config = c;
    return ODM_STATUS_OK;
}

odm_status odm_design_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                   uint64_t *out_required) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    odm_status st;
    uint32_t i, n;
    const uint64_t bytes = 2048u;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = bytes;
    if (!buffer || capacity < bytes) return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, (size_t)bytes);
    st = odm_wire_writer_init(&w, buffer, bytes);
    if (st != ODM_STATUS_OK) return st;
#define DP(x) do { st = (x); if (st != ODM_STATUS_OK) return st; } while (0)
    DP(odm_wire_write_bytes(&w, "ODMDSGN1", 8u));
    DP(odm_wire_write_u32(&w, ODM_DESIGN_POLICY_VERSION));
    DP(odm_wire_write_u32(&w, ODM_DESIGN_SCHEMA_VERSION));
    DP(odm_wire_write_u32(&w, ODM_DESIGN_CAT_COUNT));
    DP(odm_wire_write_u32(&w, ODM_DESIGN_KIND_COUNT));
    DP(odm_wire_write_u32(&w, ODM_DESIGN_FIELD_COUNT));
    DP(odm_wire_write_u32(&w, odm_template_count()));
    DP(odm_wire_write_u32(&w, 1u)); /* la plantilla pierde autoridad al cargarse */
    DP(odm_wire_write_u32(&w, 1u)); /* cada control escribe en una sola categoria */
    DP(odm_wire_write_u32(&w, 1u)); /* fuera de rango falla, no se recorta */
    DP(odm_wire_write_u32(&w, 1u)); /* la puerta de contraste cierra la compilacion */
    DP(odm_wire_write_u32(&w, 1u)); /* los colores de diseno son sRGB y se linealizan al compilar */
    n = odm_design_control_count();
    DP(odm_wire_write_u32(&w, n));
    for (i = 0u; i < n; ++i) {
        const dz_control *c = &dz_table[i];
        DP(odm_wire_write_u32(&w, c->id));
        DP(odm_wire_write_u32(&w, c->category));
        DP(odm_wire_write_u32(&w, c->kind));
        DP(odm_wire_write_u32(&w, c->min_value));
        DP(odm_wire_write_u32(&w, c->max_value));
        DP(odm_wire_write_u32(&w, c->default_value));
        DP(odm_wire_write_u32(&w, c->option_count));
    }
#undef DP
    return odm_wire_writer_finish(&w, out_required);
}

/* --- Manifiesto ------------------------------------------------------------ */

typedef struct { char *buf; uint64_t cap, used; } dz_json;

static void dz_put(dz_json *j, const char *txt) {
    while (*txt) {
        if (j->buf && j->used < j->cap) j->buf[j->used] = *txt;
        ++j->used; ++txt;
    }
}

static void dz_put_num(dz_json *j, uint32_t v) {
    char tmp[12];
    int n = 0, k;
    if (v == 0u) { dz_put(j, "0"); return; }
    while (v != 0u && n < 11) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    for (k = n - 1; k >= 0; --k) {
        if (j->buf && j->used < j->cap) j->buf[j->used] = tmp[k];
        ++j->used;
    }
}

/* Cadena JSON. Todo texto del motor es ASCII imprimible por contrato, asi que
 * solo hay que escapar comilla y barra invertida. */
static void dz_put_str(dz_json *j, const char *txt) {
    dz_put(j, "\"");
    while (*txt) {
        if (*txt == '"' || *txt == '\\') dz_put(j, "\\");
        if (j->buf && j->used < j->cap) j->buf[j->used] = *txt;
        ++j->used; ++txt;
    }
    dz_put(j, "\"");
}

static void dz_field(dz_json *j, const char *name, uint32_t v, int last) {
    dz_put_str(j, name); dz_put(j, ":"); dz_put_num(j, v);
    if (!last) dz_put(j, ",");
}

odm_status odm_design_manifest_json(char *buffer, uint64_t capacity,
                                    uint64_t *out_required) {
    dz_json j;
    uint32_t i, n, cat;
    static const char *const kinds[ODM_DESIGN_KIND_COUNT] =
        { "toggle", "enum", "escalar", "color" };
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    j.buf = buffer; j.cap = buffer ? capacity : 0u; j.used = 0u;
    n = odm_design_control_count();

    dz_put(&j, "{\"motor\":\"ODPAR Music\",");
    dz_put(&j, "\"q31\":2147483647,");
    dz_put(&j, "\"politicas\":{");
    dz_field(&j, "diseno", ODM_DESIGN_POLICY_VERSION, 0);
    dz_field(&j, "tema", ODM_THEME_POLICY_VERSION, 0);
    dz_field(&j, "capas", ODM_LAYERED_POLICY_VERSION, 1);
    dz_put(&j, "},");

    dz_put(&j, "\"categorias\":[");
    for (cat = 0u; cat < ODM_DESIGN_CAT_COUNT; ++cat) {
        if (cat) dz_put(&j, ",");
        dz_put(&j, "{"); dz_field(&j, "id", cat, 0);
        dz_put_str(&j, "nombre"); dz_put(&j, ":");
        dz_put_str(&j, odm_design_category_name(cat));
        dz_put(&j, "}");
    }
    dz_put(&j, "],\"controles\":[");
    for (i = 0u; i < n; ++i) {
        odm_design_control c;
        if (odm_design_control_at(i, &c) != ODM_STATUS_OK) return ODM_STATUS_INVARIANT_BROKEN;
        if (i) dz_put(&j, ",");
        dz_put(&j, "{");
        dz_put_str(&j, "clave"); dz_put(&j, ":"); dz_put_str(&j, c.key); dz_put(&j, ",");
        dz_put_str(&j, "etiqueta"); dz_put(&j, ":"); dz_put_str(&j, c.label); dz_put(&j, ",");
        dz_put_str(&j, "tipo"); dz_put(&j, ":"); dz_put_str(&j, kinds[c.kind]); dz_put(&j, ",");
        dz_field(&j, "categoria", c.category, 0);
        dz_field(&j, "id", c.id, 0);
        if (c.kind == ODM_DESIGN_KIND_COLOR) {
            dz_put_str(&j, "formato"); dz_put(&j, ":"); dz_put_str(&j, "#rrggbb");
        } else {
            dz_field(&j, "min", c.min_value, 0);
            dz_field(&j, "max", c.max_value, 0);
            dz_field(&j, "defecto", c.default_value, c.kind != ODM_DESIGN_KIND_ENUM);
            if (c.kind == ODM_DESIGN_KIND_ENUM) {
                uint32_t o;
                dz_put_str(&j, "opciones"); dz_put(&j, ":[");
                for (o = 0u; o < c.option_count; ++o) {
                    if (o) dz_put(&j, ",");
                    dz_put_str(&j, odm_design_option_label(c.id, o));
                }
                dz_put(&j, "]");
            }
        }
        dz_put(&j, "}");
    }
    dz_put(&j, "],\"plantillas\":[");
    for (i = 0u; i < odm_template_count(); ++i) {
        if (i) dz_put(&j, ",");
        dz_put(&j, "{"); dz_field(&j, "id", i, 0);
        dz_put_str(&j, "nombre"); dz_put(&j, ":"); dz_put_str(&j, odm_template_name(i)); dz_put(&j, ",");
        dz_put_str(&j, "resumen"); dz_put(&j, ":"); dz_put_str(&j, odm_template_summary(i));
        dz_put(&j, "}");
    }
    dz_put(&j, "],\"temas\":[");
    for (i = 0u; i < odm_theme_count(); ++i) {
        odm_theme t;
        odm_theme_contrast_report rep;
        if (odm_theme_builtin(i, &t) != ODM_STATUS_OK) return ODM_STATUS_INVARIANT_BROKEN;
        (void)odm_theme_validate(&t, &rep);
        if (i) dz_put(&j, ",");
        dz_put(&j, "{"); dz_field(&j, "id", i, 0);
        dz_put_str(&j, "nombre"); dz_put(&j, ":"); dz_put_str(&j, t.name); dz_put(&j, ",");
        dz_field(&j, "contraste_titulo", rep.title_on_background, 0);
        dz_field(&j, "contraste_autoria", rep.metadata_on_background, 1);
        dz_put(&j, "}");
    }
    dz_put(&j, "]}");

    *out_required = j.used + 1u;
    if (!buffer || capacity < j.used + 1u) return ODM_STATUS_BUFFER_TOO_SMALL;
    buffer[j.used] = '\0';
    return ODM_STATUS_OK;
}
