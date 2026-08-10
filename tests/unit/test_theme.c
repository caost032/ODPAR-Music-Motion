/* Temas: la puerta de contraste tiene que ser una puerta, no un adorno.
 * Contrato en odm_theme.h. */

#include "odm_theme.h"
#include "odm_visual_dynamics.h"

#include "test_harness.h"

#include <string.h>

void odm_test_theme(odm_test_context *context) {
    odm_theme t;
    odm_theme_contrast_report rep;
    uint32_t i, n;

    n = odm_theme_count();
    ODM_TEST_CHECK(context, n >= 3u);

    /* TODOS los temas integrados deben pasar la puerta. Si uno no pasa, no es
     * un tema: es un error que llegaria al usuario como un video ilegible. */
    for (i = 0u; i < n; ++i) {
        ODM_TEST_CHECK(context, odm_theme_builtin(i, &t) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_theme_builtin_name(i) != NULL);
        ODM_TEST_CHECK(context, t.name[0] != '\0');
        ODM_TEST_CHECK(context, odm_theme_validate(&t, &rep) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, rep.passed == 1u);
        ODM_TEST_CHECK(context, rep.failing_role == (uint32_t)ODM_THEME_ROLE_COUNT);
        ODM_TEST_CHECK(context, rep.title_on_background >= ODM_THEME_CONTRAST_TEXT_MIN);
        ODM_TEST_CHECK(context, rep.metadata_on_background >= ODM_THEME_CONTRAST_LARGE_MIN);
        ODM_TEST_CHECK(context, rep.accent_on_background >= ODM_THEME_CONTRAST_GRAPHIC_MIN);
    }
    ODM_TEST_CHECK(context, odm_theme_builtin(n, &t) == ODM_STATUS_INVALID_ARGUMENT);
    ODM_TEST_CHECK(context, odm_theme_builtin_name(n) == NULL);

    /* EL CASO QUE MOTIVA TODO ESTO: fondo negro y titulo negro.
     * Coherente sobre el papel, invisible en pantalla. Debe rechazarse, y el
     * informe debe decir exactamente cual es el rol culpable. */
    ODM_TEST_CHECK(context, odm_theme_builtin(0u, &t) == ODM_STATUS_OK);
    memset(&t.role[ODM_THEME_ROLE_TEXT_PRIMARY], 0, sizeof(t.role[0]));
    t.role[ODM_THEME_ROLE_TEXT_PRIMARY].a = UINT16_MAX;
    ODM_TEST_CHECK(context, odm_theme_validate(&t, &rep) == ODM_STATUS_INVALID_DATA);
    ODM_TEST_CHECK(context, rep.passed == 0u);
    ODM_TEST_CHECK(context, rep.failing_role == (uint32_t)ODM_THEME_ROLE_TEXT_PRIMARY);

    /* Y no debe poder materializarse: si se pudiera, el rechazo seria un aviso
     * decorativo y el usuario acabaria con un video sin titulo legible. */
    {
        odm_layered_config cfg;
        ODM_TEST_CHECK(context,
            odm_theme_apply(&t, ODM_CANVAS_ASPECT_SQUARE_1_1, 512u, 512u, 30, 1u, &cfg)
                == ODM_STATUS_INVALID_DATA);
    }

    /* Texto claro sobre fondo claro: el mismo fallo por el otro extremo. */
    ODM_TEST_CHECK(context, odm_theme_builtin(0u, &t) == ODM_STATUS_OK);
    t.role[ODM_THEME_ROLE_BACKGROUND_BASE] = t.role[ODM_THEME_ROLE_TEXT_PRIMARY];
    ODM_TEST_CHECK(context, odm_theme_validate(&t, &rep) == ODM_STATUS_INVALID_DATA);

    /* Propiedades de la metrica de contraste. */
    {
        odm_rgba16 black, white, mid;
        memset(&black, 0, sizeof(black)); black.a = UINT16_MAX;
        white.r = white.g = white.b = white.a = UINT16_MAX;
        mid.r = mid.g = mid.b = 32768u; mid.a = UINT16_MAX;

        /* Blanco sobre negro es el maximo posible: 21:1 en WCAG. */
        ODM_TEST_CHECK(context, odm_theme_contrast_ratio(&white, &black) >= 2000u);
        ODM_TEST_CHECK(context, odm_theme_contrast_ratio(&white, &black) <= 2200u);
        /* Simetrica: el orden de los colores no cambia el contraste. */
        ODM_TEST_CHECK(context, odm_theme_contrast_ratio(&white, &black) ==
                                odm_theme_contrast_ratio(&black, &white));
        /* Un color contra si mismo es exactamente 1:1, nunca cero ni infinito. */
        ODM_TEST_CHECK(context, odm_theme_contrast_ratio(&white, &white) == 100u);
        ODM_TEST_CHECK(context, odm_theme_contrast_ratio(&black, &black) == 100u);
        /* Y el negro contra negro no divide por cero: es el caso que rompe una
         * implementacion ingenua sin el desplazamiento de WCAG. */
        ODM_TEST_CHECK(context, odm_theme_contrast_ratio(&mid, &mid) == 100u);
        /* Luminancia monotona y en rango. */
        ODM_TEST_CHECK(context, odm_theme_relative_luminance(&black) == 0u);
        ODM_TEST_CHECK(context, odm_theme_relative_luminance(&white) <= ODM_VD_Q31_ONE);
        ODM_TEST_CHECK(context, odm_theme_relative_luminance(&mid) >
                                odm_theme_relative_luminance(&black));
        ODM_TEST_CHECK(context, odm_theme_relative_luminance(&white) >
                                odm_theme_relative_luminance(&mid));
        /* El verde pesa mas que el rojo, y el rojo mas que el azul: BT.709. */
        {
            odm_rgba16 r, g, b;
            memset(&r, 0, sizeof(r)); memset(&g, 0, sizeof(g)); memset(&b, 0, sizeof(b));
            r.r = UINT16_MAX; r.a = UINT16_MAX;
            g.g = UINT16_MAX; g.a = UINT16_MAX;
            b.b = UINT16_MAX; b.a = UINT16_MAX;
            ODM_TEST_CHECK(context, odm_theme_relative_luminance(&g) >
                                    odm_theme_relative_luminance(&r));
            ODM_TEST_CHECK(context, odm_theme_relative_luminance(&r) >
                                    odm_theme_relative_luminance(&b));
        }
    }

    /* Materializacion en los cuatro encuadres. El mismo tema se recompone; no
     * se estira. Todas las medidas salen del lado menor, asi que ninguna queda
     * fuera del lienzo en vertical ni desaparece en horizontal. */
    {
        static const uint32_t aspects[4] = {
            ODM_CANVAS_ASPECT_SQUARE_1_1, ODM_CANVAS_ASPECT_HORIZONTAL_16_9,
            ODM_CANVAS_ASPECT_VERTICAL_9_16, ODM_CANVAS_ASPECT_VERTICAL_4_5
        };
        static const uint32_t dims[4][2] = {
            {720u, 720u}, {1280u, 720u}, {720u, 1280u}, {720u, 900u}
        };
        uint32_t a, k;
        for (k = 0u; k < n; ++k) {
            ODM_TEST_CHECK(context, odm_theme_builtin(k, &t) == ODM_STATUS_OK);
            for (a = 0u; a < 4u; ++a) {
                odm_layered_config cfg;
                ODM_TEST_CHECK(context,
                    odm_theme_apply(&t, aspects[a], dims[a][0], dims[a][1], 30, 7u, &cfg)
                        == ODM_STATUS_OK);
                ODM_TEST_CHECK(context, odm_layered_config_validate(&cfg) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context, cfg.canvas.width == dims[a][0]);
                ODM_TEST_CHECK(context, cfg.canvas.height == dims[a][1]);
                /* Ningun trazo puede degenerar a cero: una linea de grosor cero
                 * no se dibuja, y el elemento desapareceria en silencio. */
                ODM_TEST_CHECK(context, cfg.core.border_q16 > 0u);
                ODM_TEST_CHECK(context, cfg.core.feather_q16 > 0u);
                if ((cfg.field.flags & ODM_FIELD_RADIAL_BARS) != 0u)
                    ODM_TEST_CHECK(context, cfg.field.bar_width_q16 > 0u);
                if ((cfg.hud.flags & ODM_HUD_PROGRESS_BAR) != 0u)
                    ODM_TEST_CHECK(context, cfg.hud.progress_height_q16 > 0u);
            }
        }
    }

    /* SEPARACION DE CATEGORIAS: cambiar una no puede alterar otra.
     * Es lo que permite que una app exponga cada grupo de controles por
     * separado sin que tocar el fondo estropee las particulas. */
    {
        odm_layered_config a_cfg, b_cfg;
        odm_theme a_theme, b_theme;
        ODM_TEST_CHECK(context, odm_theme_builtin(0u, &a_theme) == ODM_STATUS_OK);
        b_theme = a_theme;
        b_theme.background = ODM_THEME_BG_VOID;   /* solo cambia el fondo */
        ODM_TEST_CHECK(context,
            odm_theme_apply(&a_theme, ODM_CANVAS_ASPECT_SQUARE_1_1, 512u, 512u, 30, 3u, &a_cfg) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
            odm_theme_apply(&b_theme, ODM_CANVAS_ASPECT_SQUARE_1_1, 512u, 512u, 30, 3u, &b_cfg) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, a_cfg.background.style != b_cfg.background.style);
        /* Campo, nucleo, particulas y HUD intactos. */
        ODM_TEST_CHECK(context, memcmp(&a_cfg.field, &b_cfg.field, sizeof(a_cfg.field)) == 0);
        ODM_TEST_CHECK(context, memcmp(&a_cfg.core, &b_cfg.core, sizeof(a_cfg.core)) == 0);
        ODM_TEST_CHECK(context, memcmp(&a_cfg.hud, &b_cfg.hud, sizeof(a_cfg.hud)) == 0);

        /* Y al reves: cambiar el HUD no toca el fondo ni el campo. */
        b_theme = a_theme;
        b_theme.hud_show_progress = 0u;
        ODM_TEST_CHECK(context,
            odm_theme_apply(&b_theme, ODM_CANVAS_ASPECT_SQUARE_1_1, 512u, 512u, 30, 3u, &b_cfg) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, (b_cfg.hud.flags & ODM_HUD_PROGRESS_BAR) == 0u);
        ODM_TEST_CHECK(context, memcmp(&a_cfg.background, &b_cfg.background, sizeof(a_cfg.background)) == 0);
        ODM_TEST_CHECK(context, memcmp(&a_cfg.field, &b_cfg.field, sizeof(a_cfg.field)) == 0);
    }

    /* La barra de progreso debe poder ocupar todo el ancho util. Antes estaba
     * clavada a un tercio y no habia forma de pedir mas. */
    {
        odm_layered_config cfg;
        ODM_TEST_CHECK(context, odm_theme_builtin(0u, &t) == ODM_STATUS_OK);
        t.hud_progress_width_q31 = (uint32_t)INT32_MAX;
        ODM_TEST_CHECK(context,
            odm_theme_apply(&t, ODM_CANVAS_ASPECT_SQUARE_1_1, 720u, 720u, 30, 1u, &cfg) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, cfg.hud.progress_width_q31 == (uint32_t)INT32_MAX);
    }

    /* Identidad de politica presente y estable. */
    {
        uint8_t bytes[512];
        uint64_t required = 0u;
        ODM_TEST_CHECK(context, odm_theme_policy_bytes(NULL, 0u, &required) == ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, required == 512u);
        ODM_TEST_CHECK(context, odm_theme_policy_bytes(bytes, sizeof(bytes), &required) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(bytes, "ODMTHEM1", 8u) == 0);
    }
}
