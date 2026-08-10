#ifndef ODM_DESIGN_H
#define ODM_DESIGN_H

/* Diseno: el documento editable que hay entre una plantilla y el render.
 *
 * LA CADENA
 *
 *   PLANTILLA  ->  DISENO  ->  odm_layered_config  ->  render
 *
 * Una PLANTILLA es un punto de partida completo: decide tema, fondo, gramatica
 * del campo, forma del nucleo y HUD a la vez. Un DISENO es el resultado de
 * abrir esa plantilla y poder tocar cada cosa por separado. Una configuracion
 * de capas es el parametro efectivo que consume el rasterizador.
 *
 * La plantilla NO es autoritativa. En cuanto se carga, se compila a un diseno
 * completamente explicito y nadie vuelve a consultarla: su identificador queda
 * solo como procedencia. Un preset que siguiera decidiendo cosas en tiempo de
 * render seria una segunda fuente de verdad.
 *
 * POR QUE EL MOTOR PUBLICA SU PROPIO ESQUEMA
 *
 * La app no deberia saber que existe "densidad de particulas" ni en que rango
 * vive. Si lo supiera, habria dos listas de opciones -- la del motor y la de la
 * interfaz -- que hay que mantener de acuerdo a mano, y acabarian divergiendo.
 * Es exactamente el fallo que este motor acaba de pagar en el fondo: dos rutas
 * que debian coincidir y nadie lo comprobaba.
 *
 * Aqui el motor publica la lista de controles: identificador estable, categoria,
 * tipo, rango, valor por defecto y etiquetas de las opciones. La interfaz se
 * construye recorriendo esa lista. Anadir un control al motor lo hace aparecer
 * en la app sin tocar la app.
 *
 * CATEGORIAS
 *
 * Cada control pertenece a UNA categoria y solo escribe en ella. Cambiar el
 * fondo no puede alterar las particulas, ni el HUD el campo. No es una promesa
 * de estilo: hay un test que fija cada control, lo mueve y comprueba que el
 * resto de categorias quedan byte a byte iguales.
 *
 * LEGIBILIDAD
 *
 * Compilar un diseno cuyo texto no se distingue del fondo falla. Es la misma
 * puerta de contraste que los temas, aplicada al documento ya editado: de nada
 * sirve que la plantilla fuera legible si el usuario pinto el titulo del color
 * del fondo.
 */

#include "odm_compositor.h"
#include "odm_status.h"
#include "odm_theme.h"

#include <stdint.h>

#define ODM_DESIGN_SCHEMA_VERSION UINT32_C(1)
#define ODM_DESIGN_POLICY_VERSION UINT32_C(1)
#define ODM_DESIGN_NAME_BYTES     UINT32_C(48)
#define ODM_DESIGN_KEY_BYTES      UINT32_C(32)
#define ODM_DESIGN_LABEL_BYTES    UINT32_C(40)

/* --- Categorias ----------------------------------------------------------
 * El orden es el orden en que una interfaz deberia presentarlas: de lo que mas
 * cambia la imagen a lo que la matiza. */
enum {
    ODM_DESIGN_CAT_CANVAS     = 0u,  /* encuadre                             */
    ODM_DESIGN_CAT_BACKGROUND = 1u,  /* fondo                                */
    ODM_DESIGN_CAT_CORE       = 2u,  /* imagen o video central               */
    ODM_DESIGN_CAT_FIELD      = 3u,  /* composicion del halo espectral       */
    ODM_DESIGN_CAT_PARTICLES  = 4u,  /* particulas                           */
    ODM_DESIGN_CAT_TEXT       = 5u,  /* titulo y autoria                     */
    ODM_DESIGN_CAT_PROGRESS   = 6u,  /* barra de progreso y tiempo           */
    ODM_DESIGN_CAT_MOTION     = 7u,  /* caracter de la reaccion              */
    ODM_DESIGN_CAT_COUNT      = 8u
};

/* --- Tipos de control ---------------------------------------------------- */
enum {
    ODM_DESIGN_KIND_TOGGLE = 0u,  /* 0 o 1                                   */
    ODM_DESIGN_KIND_ENUM   = 1u,  /* 0..option_count-1, con etiquetas        */
    ODM_DESIGN_KIND_SCALAR = 2u,  /* Q1.31 entre min y max                   */
    ODM_DESIGN_KIND_COLOR  = 3u,  /* odm_rgba16                              */
    ODM_DESIGN_KIND_COUNT  = 4u
};

/* --- Gramatica del campo -------------------------------------------------
 * Es el eje de composicion: no cambia el color, cambia la FORMA en que la
 * energia espectral ocupa el espacio alrededor del nucleo. */
enum {
    ODM_DESIGN_FIELD_FILAMENT = 0u, /* filamentos largos y finos             */
    ODM_DESIGN_FIELD_BARS     = 1u, /* barras marcadas y separadas           */
    ODM_DESIGN_FIELD_CORONA   = 2u, /* corona corta y densa                  */
    ODM_DESIGN_FIELD_RING     = 3u, /* anillo orbital con barras cortas      */
    ODM_DESIGN_FIELD_NONE     = 4u, /* sin halo                              */
    ODM_DESIGN_FIELD_COUNT    = 5u
};

/* --- Estructuras por categoria -------------------------------------------
 * Separadas fisicamente, no solo por convencion: la aislacion se comprueba con
 * memcmp sobre estas estructuras. */

typedef struct {
    uint32_t style;            /* ODM_BACKGROUND_*                           */
    uint32_t intensity_q31;
    uint32_t scale_q31;        /* separacion relativa al lado menor          */
    uint32_t line_q31;         /* grosor relativo                            */
    uint32_t reactivity_q31;
    odm_rgba16 color;          /* plano de fondo                             */
    odm_rgba16 accent;         /* rejilla, anillos, puntos, resplandor       */
} odm_design_background;

typedef struct {
    uint32_t shape;            /* ODM_CORE_SHAPE_*                           */
    uint32_t fit;              /* ODM_CORE_FIT_*                             */
    uint32_t size_q31;
    uint32_t corner_q31;
    uint32_t border_q31;       /* ancho del borde, relativo al lado menor    */
    uint32_t opacity_q31;
    uint32_t reactivity_q31;
    odm_rgba16 border_color;
} odm_design_core;

typedef struct {
    uint32_t grammar;          /* ODM_DESIGN_FIELD_*                         */
    uint32_t length_q31;
    uint32_t weight_q31;
    uint32_t opacity_q31;
    uint32_t detail;           /* 0 = 48 sectores, 1 = 96                    */
    odm_rgba16 color;
    odm_rgba16 accent;
} odm_design_field;

typedef struct {
    uint32_t enabled;
    uint32_t density_q31;
    uint32_t size_q31;
    odm_rgba16 color;
} odm_design_particles;

typedef struct {
    uint32_t show_title;
    uint32_t show_artist;
    uint32_t scale_q31;
    uint32_t anchor;           /* ODM_HUD_ANCHOR_*                           */
    odm_rgba16 title_color;
    odm_rgba16 artist_color;
    char title[ODM_LAYERED_METADATA_BYTES];
    char artist[ODM_LAYERED_METADATA_BYTES];
} odm_design_text;

typedef struct {
    uint32_t show_bar;
    uint32_t show_time;
    uint32_t style;            /* ODM_HUD_PROGRESS_*                         */
    uint32_t time_mode;        /* ODM_HUD_TIME_*                             */
    uint32_t width_q31;        /* Q31 = todo el ancho util                   */
    uint32_t height_q31;
    odm_rgba16 color;
    odm_rgba16 track_color;
} odm_design_progress;

typedef struct {
    uint32_t sensitivity_q31;  /* ganancia global de reaccion                */
    uint32_t attack_q31;       /* cuanto pesa el ataque                      */
    uint32_t release_q31;      /* cuanto pesa la cola                        */
} odm_design_motion;

typedef struct {
    uint32_t schema_version;
    char     name[ODM_DESIGN_NAME_BYTES];
    uint32_t template_id;      /* procedencia; no se consulta al compilar    */
    uint32_t theme_id;         /* idem                                       */
    uint32_t aspect;           /* ODM_CANVAS_ASPECT_*                        */

    odm_design_background background;
    odm_design_core       core;
    odm_design_field      field;
    odm_design_particles  particles;
    odm_design_text       text;
    odm_design_progress   progress;
    odm_design_motion     motion;

    uint32_t reserved[8];
} odm_design;

typedef struct {
    uint32_t id;
    uint32_t category;
    uint32_t kind;
    uint32_t min_value;
    uint32_t max_value;
    uint32_t default_value;
    uint32_t option_count;     /* solo ENUM                                  */
    char key[ODM_DESIGN_KEY_BYTES];
    char label[ODM_DESIGN_LABEL_BYTES];
} odm_design_control;

typedef struct {
    uint32_t schema_version;
    uint32_t passed;
    uint32_t title_contrast;   /* centesimas                                 */
    uint32_t artist_contrast;
    uint32_t field_contrast;
    uint32_t failing_category;  /* categoria culpable, o CAT_COUNT           */
    uint32_t reserved[2];
} odm_design_report;

/* --- Esquema publicado --------------------------------------------------- */

uint32_t odm_design_control_count(void);
odm_status odm_design_control_at(uint32_t index, odm_design_control *out_control);
odm_status odm_design_control_find(const char *key, odm_design_control *out_control);
/* Etiqueta de una opcion de un control ENUM, o NULL. */
const char *odm_design_option_label(uint32_t control_id, uint32_t option);
/* Nombre legible de una categoria, o NULL. */
const char *odm_design_category_name(uint32_t category);

/* --- Lectura y escritura genericas --------------------------------------
 * Un control escalar, de opcion o de conmutacion se lee y escribe como u32; los
 * de color, como odm_rgba16. Escribir fuera de rango falla: recortar en
 * silencio dejaria a la app creyendo que fijo un valor que el motor no tiene. */
odm_status odm_design_get(const odm_design *design, uint32_t control_id,
                          uint32_t *out_value);
odm_status odm_design_set(odm_design *design, uint32_t control_id, uint32_t value);
odm_status odm_design_get_color(const odm_design *design, uint32_t control_id,
                                odm_rgba16 *out_color);
odm_status odm_design_set_color(odm_design *design, uint32_t control_id,
                                const odm_rgba16 *color);

/* --- Plantillas ---------------------------------------------------------- */

uint32_t odm_template_count(void);
const char *odm_template_name(uint32_t index);
/* Descripcion corta de lo que decide la plantilla. */
const char *odm_template_summary(uint32_t index);
/* Carga una plantilla como diseno completamente explicito. */
odm_status odm_template_load(uint32_t index, uint32_t aspect, odm_design *out_design);

/* Deriva un diseno de un tema. Es el puente entre la identidad visual (tema) y
 * el documento editable (diseno). */
odm_status odm_design_from_theme(const odm_theme *theme, uint32_t aspect,
                                 odm_design *out_design);

/* --- Validacion y compilacion -------------------------------------------- */

/* Rango de todos los controles mas la puerta de contraste sobre el documento ya
 * editado. El informe se rellena tanto si pasa como si no. */
odm_status odm_design_validate(const odm_design *design, odm_design_report *out_report);

/* Compila a parametros efectivos. Falla cerrado si la validacion falla: un
 * diseno ilegible no puede convertirse en un render. */
odm_status odm_design_compile(const odm_design *design,
                              uint32_t width, uint32_t height,
                              int32_t fps, uint64_t seed,
                              odm_layered_config *out_config);

/* Metadatos de texto. Separado de la compilacion porque titulo y autoria son
 * datos del proyecto, no decisiones de diseno. */
odm_status odm_design_set_metadata(odm_design *design, const char *title,
                                   const char *artist);

odm_status odm_design_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                   uint64_t *out_required);

#endif /* ODM_DESIGN_H */
