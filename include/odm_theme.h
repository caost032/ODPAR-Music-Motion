#ifndef ODM_THEME_H
#define ODM_THEME_H

/* Temas: identidad visual completa y coherente, con legibilidad demostrada.
 *
 * Un tema NO es una lista de colores. Si lo fuera, nada impediria un tema con
 * fondo negro y titulo negro: coherente sobre el papel, invisible en pantalla.
 * Aqui un tema es un conjunto de ROLES semanticos mas un contrato de contraste
 * que el motor comprueba y hace cumplir. Un tema que no separa suficientemente
 * el texto del fondo NO SE ACEPTA -- no se corrige en silencio, se rechaza.
 *
 * SEPARACION POR CATEGORIAS
 *
 * Cada familia de opciones vive en su propia categoria y no se mezcla con otra.
 * El fondo no decide nada sobre las particulas; las particulas no deciden nada
 * sobre el HUD. Una app puede exponer cada categoria como un grupo de controles
 * independiente, y cambiar una no puede corromper otra:
 *
 *   PALETA      roles de color, y solo eso
 *   FONDO       un unico estilo elegido entre varios, con sus parametros
 *   NUCLEO      forma y tratamiento de la imagen o video central
 *   CAMPO       gramatica del halo espectral
 *   PARTICULAS  densidad y caracter
 *   HUD         que informacion se muestra y como
 *   MOVIMIENTO  caracter temporal de las respuestas
 *
 * CONTRASTE
 *
 * La luminancia relativa se calcula en luz LINEAL, que es el dominio en el que
 * el motor ya trabaja, con los coeficientes BT.709. El ratio sigue la formula
 * de WCAG 2.x: (L_claro + 0.05) / (L_oscuro + 0.05).
 */

#include "odm_compositor.h"
#include "odm_status.h"

#include <stdint.h>

#define ODM_THEME_SCHEMA_VERSION UINT32_C(1)
#define ODM_THEME_POLICY_VERSION UINT32_C(2)
#define ODM_THEME_NAME_BYTES     UINT32_C(48)

/* Umbrales de contraste en centesimas, para no usar coma flotante.
 * 450 = 4.5:1, el minimo de WCAG AA para texto normal.
 * 300 = 3.0:1, el minimo para texto grande y elementos graficos. */
#define ODM_THEME_CONTRAST_TEXT_MIN      UINT32_C(450)
#define ODM_THEME_CONTRAST_LARGE_MIN     UINT32_C(300)
#define ODM_THEME_CONTRAST_GRAPHIC_MIN   UINT32_C(300)

/* --- CATEGORIA: PALETA ---------------------------------------------------
 * Roles semanticos, no nombres de color. "accent" significa lo que acentua,
 * no "naranja": cambiar el tema cambia el color sin cambiar su funcion. */
enum {
    ODM_THEME_ROLE_BACKGROUND_DEEP = 0u,  /* el vacio mas alejado            */
    ODM_THEME_ROLE_BACKGROUND_BASE = 1u,  /* plano de fondo dominante        */
    ODM_THEME_ROLE_SURFACE         = 2u,  /* superficies y contornos suaves  */
    ODM_THEME_ROLE_PRIMARY         = 3u,  /* cuerpo espectral sostenido      */
    ODM_THEME_ROLE_SECONDARY       = 4u,  /* estructura secundaria           */
    ODM_THEME_ROLE_ACCENT          = 5u,  /* ataque: lo unico que se ilumina */
    ODM_THEME_ROLE_TEXT_PRIMARY    = 6u,  /* titulo                          */
    ODM_THEME_ROLE_TEXT_SECONDARY  = 7u,  /* autoria, metadatos, tiempo      */
    ODM_THEME_ROLE_EVENT_HIGHLIGHT = 8u,  /* picos, destellos causales       */
    ODM_THEME_ROLE_COUNT           = 9u
};

/* --- CATEGORIA: FONDO ----------------------------------------------------
 * Se elige UNO. Nunca se combinan: dos fondos a la vez es ruido, no riqueza. */
enum {
    ODM_THEME_BG_VOID           = 0u, /* negro puro                          */
    ODM_THEME_BG_DEPTH_FIELD    = 1u, /* halo espacial radial difuminado     */
    ODM_THEME_BG_PERSPECTIVE    = 2u, /* rejilla en fuga                     */
    ODM_THEME_BG_FLAT_GRID      = 3u, /* rejilla plana                       */
    ODM_THEME_BG_SOLID          = 4u, /* plano de color                      */
    ODM_THEME_BG_COUNT          = 5u
};

/* --- CATEGORIA: CAMPO (halo) --------------------------------------------- */
enum {
    ODM_THEME_FIELD_FILAMENT = 0u,  /* filamentos finos, leen como una forma */
    ODM_THEME_FIELD_BARS     = 1u,  /* barras marcadas y separadas           */
    ODM_THEME_FIELD_CORONA   = 2u,  /* corona densa y corta                  */
    ODM_THEME_FIELD_NONE     = 3u,  /* sin halo: el nucleo manda solo        */
    ODM_THEME_FIELD_COUNT    = 4u
};

/* --- CATEGORIA: NUCLEO --------------------------------------------------- */
enum {
    ODM_THEME_CORE_CIRCLE  = 0u,
    ODM_THEME_CORE_ROUNDED = 1u,
    ODM_THEME_CORE_SQUARE  = 2u,
    ODM_THEME_CORE_COUNT   = 3u
};

typedef struct {
    uint32_t schema_version;
    char     name[ODM_THEME_NAME_BYTES];

    /* PALETA */
    odm_rgba16 role[ODM_THEME_ROLE_COUNT];

    /* FONDO */
    uint32_t background;          /* ODM_THEME_BG_*                          */
    uint32_t background_intensity_q31; /* cuanto pesa el fondo en la escena  */

    /* NUCLEO */
    uint32_t core_shape;          /* ODM_THEME_CORE_*                        */
    uint32_t core_size_q31;       /* fraccion del lienzo                     */
    uint32_t core_border_q31;     /* grosor relativo del contorno            */

    /* CAMPO */
    uint32_t field;               /* ODM_THEME_FIELD_*                       */
    uint32_t field_length_q31;    /* alcance radial relativo                 */
    uint32_t field_weight_q31;    /* grosor relativo del trazo               */

    /* PARTICULAS */
    uint32_t particle_density_q31; /* 0 = ninguna                            */

    /* HUD: cada elemento se activa por separado */
    uint32_t hud_show_progress;
    uint32_t hud_show_time;
    uint32_t hud_show_title;
    uint32_t hud_show_artist;
    uint32_t hud_progress_width_q31; /* 0 = fino, Q31 = todo el ancho util   */
    uint32_t hud_text_scale_q31;     /* tamano tipografico relativo          */

    uint32_t reserved[6];
} odm_theme;

/* Informe de contraste. Se publica siempre, tanto si el tema pasa como si no:
 * un tema rechazado debe poder explicar POR QUE. */
typedef struct {
    uint32_t schema_version;
    uint32_t passed;                 /* 0 si algun par no alcanza su minimo  */
    uint32_t title_on_background;    /* ratios en centesimas                 */
    uint32_t metadata_on_background;
    uint32_t accent_on_background;
    uint32_t primary_on_background;
    uint32_t secondary_on_background;
    uint32_t failing_role;           /* primer rol que incumple, o COUNT     */
    uint32_t reserved[2];
} odm_theme_contrast_report;

/* Numero de temas integrados. */
uint32_t odm_theme_count(void);

/* Carga un tema integrado por indice. */
odm_status odm_theme_builtin(uint32_t index, odm_theme *out_theme);

/* Nombre legible de un tema integrado, o NULL. */
const char *odm_theme_builtin_name(uint32_t index);

/* Convierte un color de ESPACIO DE AUTOR (codigos sRGB de 16 bits) a ESPACIO DE
 * RENDER (luz lineal de 16 bits).
 *
 * La distincion no es academica. El compositor mezcla y compone en luz lineal
 * -- su codificacion final es linear_u16 -> sRGB8 -- mientras que un tema se
 * escribe en codigos sRGB, que es como piensa quien elige un color y como lo
 * exige WCAG para el contraste. Sin esta conversion el mismo numero significa
 * dos cosas distintas en dos capas: un gris de autor #23262B se renderizaba
 * como si fuera luz lineal 0.137, es decir mucho mas claro de lo escrito, y la
 * puerta de contraste garantizaba algo que no era lo que se veia.
 *
 * El alfa no se convierte: es cobertura, y la cobertura ya es lineal. */
void odm_theme_srgb16_to_linear16(const odm_rgba16 *in_srgb, odm_rgba16 *out_linear);

/* Luminancia relativa BT.709 de un color, en Q1.31 sobre luz lineal. */
uint32_t odm_theme_relative_luminance(const odm_rgba16 *color);

/* Ratio de contraste entre dos colores, en centesimas (450 = 4.5:1). */
uint32_t odm_theme_contrast_ratio(const odm_rgba16 *a, const odm_rgba16 *b);

/* Comprueba la legibilidad de un tema. Devuelve ODM_STATUS_OK solo si TODOS
 * los pares alcanzan su minimo; el informe se rellena en ambos casos. */
odm_status odm_theme_validate(const odm_theme *theme,
                              odm_theme_contrast_report *out_report);

/* Materializa un tema en una configuracion de capas concreta.
 *
 * `aspect` es uno de ODM_CANVAS_ASPECT_*, de modo que el mismo tema se
 * recompone para 1:1, 16:9, 9:16 o 4:5 en lugar de estirarse. Todas las
 * medidas se derivan del lado menor del lienzo, nunca de pixeles fijos.
 *
 * Falla cerrado si el tema no pasa la validacion de contraste: materializar un
 * tema ilegible seria producir un video en el que no se lee el titulo. */
odm_status odm_theme_apply(const odm_theme *theme,
                           uint32_t aspect, uint32_t width, uint32_t height,
                           int32_t fps, uint64_t seed,
                           odm_layered_config *out_config);

/* Bytes canonicos de la politica de temas y su digest. */
odm_status odm_theme_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                  uint64_t *out_required);

#endif /* ODM_THEME_H */
