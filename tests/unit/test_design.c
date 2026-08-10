/* Plantillas, esquema de controles y separacion de categorias.
 * Contrato en odm_design.h. */

#include "odm_design.h"

#include "test_harness.h"

#include <stdlib.h>
#include <string.h>

/* Vista por categoria del documento. La aislacion se comprueba sobre estas
 * regiones: si un control escribe fuera de la suya, un memcmp lo detecta. */
static void cat_region(const odm_design *d, uint32_t category,
                       const void **out_ptr, size_t *out_size) {
    switch (category) {
        case ODM_DESIGN_CAT_CANVAS:     *out_ptr = &d->aspect;     *out_size = sizeof(d->aspect); break;
        case ODM_DESIGN_CAT_BACKGROUND: *out_ptr = &d->background; *out_size = sizeof(d->background); break;
        case ODM_DESIGN_CAT_CORE:       *out_ptr = &d->core;       *out_size = sizeof(d->core); break;
        case ODM_DESIGN_CAT_FIELD:      *out_ptr = &d->field;      *out_size = sizeof(d->field); break;
        case ODM_DESIGN_CAT_PARTICLES:  *out_ptr = &d->particles;  *out_size = sizeof(d->particles); break;
        case ODM_DESIGN_CAT_TEXT:       *out_ptr = &d->text;       *out_size = sizeof(d->text); break;
        case ODM_DESIGN_CAT_PROGRESS:   *out_ptr = &d->progress;   *out_size = sizeof(d->progress); break;
        default:                        *out_ptr = &d->motion;     *out_size = sizeof(d->motion); break;
    }
}

void odm_test_design(odm_test_context *context) {
    uint32_t i, j, n, nt;

    n = odm_design_control_count();
    nt = odm_template_count();
    ODM_TEST_CHECK(context, n >= 30u);
    ODM_TEST_CHECK(context, nt >= 4u);

    /* --- COHERENCIA DEL ESQUEMA PUBLICADO ---------------------------------
     * La app construye su interfaz recorriendo esta lista. Un esquema
     * incoherente produce una interfaz incoherente, y el usuario lo vive como
     * un control que no hace nada. */
    for (i = 0u; i < n; ++i) {
        odm_design_control a;
        ODM_TEST_CHECK(context, odm_design_control_at(i, &a) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, a.category < ODM_DESIGN_CAT_COUNT);
        ODM_TEST_CHECK(context, a.kind < ODM_DESIGN_KIND_COUNT);
        ODM_TEST_CHECK(context, a.key[0] != '\0' && a.label[0] != '\0');
        ODM_TEST_CHECK(context, odm_design_category_name(a.category) != NULL);

        if (a.kind != ODM_DESIGN_KIND_COLOR) {
            ODM_TEST_CHECK(context, a.min_value <= a.default_value);
            ODM_TEST_CHECK(context, a.default_value <= a.max_value);
        }
        if (a.kind == ODM_DESIGN_KIND_ENUM) {
            /* Toda opcion tiene etiqueta, y solo esas. Un hueco dejaria a la
             * interfaz mostrando una opcion sin nombre. */
            ODM_TEST_CHECK(context, a.option_count >= 2u);
            ODM_TEST_CHECK(context, a.max_value == a.option_count - 1u);
            for (j = 0u; j < a.option_count; ++j)
                ODM_TEST_CHECK(context, odm_design_option_label(a.id, j) != NULL);
            ODM_TEST_CHECK(context, odm_design_option_label(a.id, a.option_count) == NULL);
        } else {
            ODM_TEST_CHECK(context, a.option_count == 0u);
        }

        /* Claves e identificadores unicos: son la referencia estable con la que
         * un proyecto guardado vuelve a encontrar sus valores. */
        for (j = i + 1u; j < n; ++j) {
            odm_design_control b;
            ODM_TEST_CHECK(context, odm_design_control_at(j, &b) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, a.id != b.id);
            ODM_TEST_CHECK(context, strcmp(a.key, b.key) != 0);
        }

        /* Buscar por clave devuelve el mismo control. */
        {
            odm_design_control f;
            ODM_TEST_CHECK(context, odm_design_control_find(a.key, &f) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, f.id == a.id && f.category == a.category);
        }
    }
    {
        odm_design_control tmp;
        ODM_TEST_CHECK(context, odm_design_control_at(n, &tmp) == ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context, odm_design_control_find("no.existe", &tmp) == ODM_STATUS_INVALID_ARGUMENT);
    }

    /* --- LAS PLANTILLAS SON PUNTOS DE PARTIDA VALIDOS ---------------------- */
    for (i = 0u; i < nt; ++i) {
        odm_design d;
        odm_design_report rep;
        ODM_TEST_CHECK(context, odm_template_name(i) != NULL);
        ODM_TEST_CHECK(context, odm_template_summary(i) != NULL);
        ODM_TEST_CHECK(context,
            odm_template_load(i, 0u, &d) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, d.template_id == i);
        ODM_TEST_CHECK(context, d.name[0] != '\0');
        ODM_TEST_CHECK(context, odm_design_validate(&d, &rep) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, rep.passed == 1u);
        ODM_TEST_CHECK(context, rep.failing_category == ODM_DESIGN_CAT_COUNT);

        /* Y compilan en los cuatro encuadres, no solo en el cuadrado. */
        {
            static const uint32_t dims[5][2] = {
                {720u, 720u}, {1280u, 720u}, {720u, 1280u}, {720u, 900u}, {960u, 720u}
            };
            uint32_t a;
            for (a = 0u; a < 5u; ++a) {
                odm_layered_config cfg;
                odm_design da = d;
                da.aspect = a;
                ODM_TEST_CHECK(context,
                    odm_design_compile(&da, dims[a][0], dims[a][1], 30, 11u, &cfg)
                        == ODM_STATUS_OK);
                ODM_TEST_CHECK(context, odm_layered_config_validate(&cfg) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context, cfg.canvas.width == dims[a][0]);
                ODM_TEST_CHECK(context, cfg.canvas.height == dims[a][1]);
                /* Ningun trazo puede degenerar a cero: lo que mide cero no se
                 * dibuja y el elemento desaparece sin aviso. */
                ODM_TEST_CHECK(context, cfg.core.border_q16 > 0u);
                ODM_TEST_CHECK(context, cfg.core.feather_q16 > 0u);
                if ((cfg.field.flags & ODM_FIELD_RADIAL_BARS) != 0u)
                    ODM_TEST_CHECK(context, cfg.field.bar_width_q16 > 0u);
                if ((cfg.hud.flags & ODM_HUD_PROGRESS_BAR) != 0u)
                    ODM_TEST_CHECK(context, cfg.hud.progress_height_q16 > 0u);
            }
        }
    }
    {
        odm_design d;
        ODM_TEST_CHECK(context, odm_template_load(nt, 0u, &d) == ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context, odm_template_name(nt) == NULL);
    }

    /* --- SEPARACION DE CATEGORIAS -----------------------------------------
     *
     * La propiedad central: mover UN control cambia SU categoria y ninguna
     * otra. Se comprueba control a control, no con un par de ejemplos: es lo
     * que permite que una app exponga cada grupo por separado sabiendo que
     * tocar el fondo no puede estropear las particulas. */
    for (i = 0u; i < n; ++i) {
        odm_design base, mod;
        odm_design_control ctl;
        uint32_t k;
        ODM_TEST_CHECK(context, odm_design_control_at(i, &ctl) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_template_load(0u, 0u, &base) == ODM_STATUS_OK);
        mod = base;

        if (ctl.kind == ODM_DESIGN_KIND_COLOR) {
            odm_rgba16 antes, otro;
            ODM_TEST_CHECK(context, odm_design_get_color(&base, ctl.id, &antes) == ODM_STATUS_OK);
            otro.r = (uint16_t)(antes.r ^ 0x3f3fu);
            otro.g = (uint16_t)(antes.g ^ 0x1717u);
            otro.b = (uint16_t)(antes.b ^ 0x2b2bu);
            otro.a = UINT16_MAX;
            ODM_TEST_CHECK(context, odm_design_set_color(&mod, ctl.id, &otro) == ODM_STATUS_OK);
            /* Y un control de color no se deja leer como escalar ni al reves:
             * confundirlos escribiria basura en el documento. */
            {
                uint32_t tmp = 0u;
                ODM_TEST_CHECK(context, odm_design_get(&base, ctl.id, &tmp) == ODM_STATUS_INVALID_ARGUMENT);
                ODM_TEST_CHECK(context, odm_design_set(&mod, ctl.id, 0u) == ODM_STATUS_INVALID_ARGUMENT);
            }
        } else {
            uint32_t antes = 0u, otro;
            odm_rgba16 ctmp;
            ODM_TEST_CHECK(context, odm_design_get(&base, ctl.id, &antes) == ODM_STATUS_OK);
            if (ctl.kind == ODM_DESIGN_KIND_ENUM)
                otro = (antes + 1u) % ctl.option_count;
            else if (ctl.kind == ODM_DESIGN_KIND_TOGGLE)
                otro = antes ^ 1u;
            else
                otro = (antes != ctl.max_value) ? ctl.max_value : ctl.min_value;
            ODM_TEST_CHECK(context, odm_design_set(&mod, ctl.id, otro) == ODM_STATUS_OK);
            {
                uint32_t leido = 0u;
                ODM_TEST_CHECK(context, odm_design_get(&mod, ctl.id, &leido) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context, leido == otro);
            }
            ODM_TEST_CHECK(context, odm_design_get_color(&base, ctl.id, &ctmp) == ODM_STATUS_INVALID_ARGUMENT);

            /* Fuera de rango falla y NO deja rastro. Un recorte silencioso
             * dejaria a la interfaz mostrando un valor que el motor no tiene. */
            if (ctl.max_value != UINT32_MAX) {
                odm_design intacto = mod;
                uint32_t leido = 0u;
                ODM_TEST_CHECK(context,
                    odm_design_set(&mod, ctl.id, ctl.max_value + 1u) == ODM_STATUS_INVALID_DATA);
                ODM_TEST_CHECK(context, memcmp(&intacto, &mod, sizeof(mod)) == 0);
                ODM_TEST_CHECK(context, odm_design_get(&mod, ctl.id, &leido) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context, leido == otro);
            }
        }

        for (k = 0u; k < ODM_DESIGN_CAT_COUNT; ++k) {
            const void *pa; const void *pb; size_t sa, sb;
            cat_region(&base, k, &pa, &sa);
            cat_region(&mod, k, &pb, &sb);
            if (k == ctl.category) continue;
            ODM_TEST_CHECK(context, sa == sb && memcmp(pa, pb, sa) == 0);
        }
    }

    /* --- LA PUERTA DE CONTRASTE, SOBRE EL DOCUMENTO YA EDITADO -------------
     *
     * Que la plantilla fuera legible no basta: el usuario puede pintar el
     * titulo del color del fondo despues. Es exactamente el caso que hace que
     * los temas no puedan aplicarse "a ciegas". */
    {
        odm_design d;
        odm_design_report rep;
        odm_design_control ctl;
        odm_layered_config cfg;
        odm_rgba16 fondo;
        ODM_TEST_CHECK(context, odm_template_load(0u, 0u, &d) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_control_find("texto.titulo", &ctl) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_set(&d, ctl.id, 1u) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_set_metadata(&d, "PRUEBA", "AUTOR") == ODM_STATUS_OK);

        ODM_TEST_CHECK(context, odm_design_control_find("fondo.color", &ctl) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_get_color(&d, ctl.id, &fondo) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_control_find("texto.titulo_color", &ctl) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_set_color(&d, ctl.id, &fondo) == ODM_STATUS_OK);

        ODM_TEST_CHECK(context, odm_design_validate(&d, &rep) == ODM_STATUS_INVALID_DATA);
        ODM_TEST_CHECK(context, rep.passed == 0u);
        ODM_TEST_CHECK(context, rep.failing_category == ODM_DESIGN_CAT_TEXT);
        /* Y no puede materializarse: si pudiera, el rechazo seria decorativo. */
        ODM_TEST_CHECK(context,
            odm_design_compile(&d, 512u, 512u, 30, 1u, &cfg) == ODM_STATUS_INVALID_DATA);

        /* Con el titulo oculto, ese mismo color deja de ser un problema: se
         * comprueba lo que se dibuja, no lo que esta guardado. */
        ODM_TEST_CHECK(context, odm_design_control_find("texto.titulo", &ctl) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_set(&d, ctl.id, 0u) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_validate(&d, &rep) == ODM_STATUS_OK);
    }

    /* --- LA BARRA DE PROGRESO LLEGA AL ANCHO COMPLETO ---------------------- */
    {
        odm_design d;
        odm_design_control ctl;
        odm_layered_config cfg;
        ODM_TEST_CHECK(context, odm_template_load(0u, 0u, &d) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_control_find("progreso.ancho", &ctl) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, ctl.max_value == (uint32_t)INT32_MAX);
        ODM_TEST_CHECK(context, odm_design_set(&d, ctl.id, ctl.max_value) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_compile(&d, 720u, 720u, 30, 1u, &cfg) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, cfg.hud.progress_width_q31 == (uint32_t)INT32_MAX);
    }

    /* --- LAS CATEGORIAS SIGUEN SEPARADAS EN LA CONFIGURACION COMPILADA -----
     *
     * La aislacion del documento no sirve de nada si al compilar se vuelven a
     * mezclar. Se comprueba en los dos sitios. */
    {
        odm_design a, b;
        odm_design_control ctl;
        odm_layered_config ca, cb;
        ODM_TEST_CHECK(context, odm_template_load(0u, 0u, &a) == ODM_STATUS_OK);
        b = a;
        ODM_TEST_CHECK(context, odm_design_control_find("fondo.estilo", &ctl) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_set(&b, ctl.id, ODM_BACKGROUND_DOT_MATRIX) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_compile(&a, 512u, 512u, 30, 5u, &ca) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_compile(&b, 512u, 512u, 30, 5u, &cb) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, ca.background.style != cb.background.style);
        ODM_TEST_CHECK(context, memcmp(&ca.field, &cb.field, sizeof(ca.field)) == 0);
        ODM_TEST_CHECK(context, memcmp(&ca.core, &cb.core, sizeof(ca.core)) == 0);
        ODM_TEST_CHECK(context, memcmp(&ca.hud, &cb.hud, sizeof(ca.hud)) == 0);

        /* El color de las particulas es SUYO: antes compartia campo con el
         * acento del halo, de modo que retocar uno movia el otro. */
        b = a;
        ODM_TEST_CHECK(context, odm_design_control_find("particulas.color", &ctl) == ODM_STATUS_OK);
        {
            odm_rgba16 verde; verde.r = 0u; verde.g = UINT16_MAX; verde.b = 0u; verde.a = UINT16_MAX;
            ODM_TEST_CHECK(context, odm_design_set_color(&b, ctl.id, &verde) == ODM_STATUS_OK);
        }
        ODM_TEST_CHECK(context, odm_design_compile(&b, 512u, 512u, 30, 5u, &cb) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(&ca.field.primary_color, &cb.field.primary_color,
                                       sizeof(ca.field.primary_color)) == 0);
        ODM_TEST_CHECK(context, memcmp(&ca.field.secondary_color, &cb.field.secondary_color,
                                       sizeof(ca.field.secondary_color)) == 0);
        ODM_TEST_CHECK(context, memcmp(&ca.field.particle_color, &cb.field.particle_color,
                                       sizeof(ca.field.particle_color)) != 0);

        /* Y el titulo tiene color propio, distinto del de la barra. */
        b = a;
        ODM_TEST_CHECK(context, odm_design_control_find("texto.titulo_color", &ctl) == ODM_STATUS_OK);
        {
            odm_rgba16 rojo; rojo.r = UINT16_MAX; rojo.g = 0u; rojo.b = 0u; rojo.a = UINT16_MAX;
            ODM_TEST_CHECK(context, odm_design_set_color(&b, ctl.id, &rojo) == ODM_STATUS_OK);
        }
        ODM_TEST_CHECK(context, odm_design_compile(&b, 512u, 512u, 30, 5u, &cb) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(&ca.hud.progress_color, &cb.hud.progress_color,
                                       sizeof(ca.hud.progress_color)) == 0);
        ODM_TEST_CHECK(context, memcmp(&ca.hud.title_color, &cb.hud.title_color,
                                       sizeof(ca.hud.title_color)) != 0);
    }

    /* --- TODO TEMA PRODUCE UN DISENO VALIDO -------------------------------- */
    {
        uint32_t th, tn = odm_theme_count();
        for (th = 0u; th < tn; ++th) {
            odm_theme t;
            odm_design d;
            odm_design_report rep;
            ODM_TEST_CHECK(context, odm_theme_builtin(th, &t) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_design_from_theme(&t, 0u, &d) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_design_validate(&d, &rep) == ODM_STATUS_OK);
        }
    }

    /* --- CAMBIAR DE COMPOSICION NO CAMBIA LA REACCION -----------------------
     *
     * Esta es la propiedad sobre la que se sostiene todo el catalogo futuro.
     *
     * Si elegir otro fondo, otra gramatica de halo, otra forma de barra o otra
     * forma de nucleo alterase la reaccion a la musica, cada composicion nueva
     * habria que reajustarla a mano contra la musica, y anadir la decima
     * costaria lo mismo que costo la primera. Como NO la altera, anadir
     * composiciones es solo dibujar: la reaccion ya esta resuelta y es la
     * misma para todas.
     *
     * Se comprueba sobre el plan de cuadro, que es donde la evidencia musical
     * se vuelve numeros que el rasterizador consume: dos disenos que solo
     * difieren en decisiones de apariencia tienen que producir exactamente los
     * mismos valores de reaccion sobre la misma muestra. */
    {
        odm_design a, b;
        odm_design_control ctl;
        odm_layered_config ca, cb;
        odm_layered_frame_plan pa, pb;
        odm_composition_frame_state comp;
        odm_director_frame_state dir;
        uint32_t k;

        ODM_TEST_CHECK(context, odm_template_load(0u, 0u, &a) == ODM_STATUS_OK);
        b = a;
        /* Solo apariencia: fondo, gramatica del halo, forma de barra y forma
         * del nucleo. Ni una sola decision de reaccion. */
        ODM_TEST_CHECK(context, odm_design_control_find("fondo.estilo", &ctl) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_set(&b, ctl.id, ODM_BACKGROUND_DOT_MATRIX) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_control_find("campo.gramatica", &ctl) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_set(&b, ctl.id, ODM_DESIGN_FIELD_BARS) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_control_find("campo.forma", &ctl) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_set(&b, ctl.id, ODM_FIELD_BAR_DOTS) == ODM_STATUS_OK);

        ODM_TEST_CHECK(context, odm_design_compile(&a, 512u, 512u, 30, 9u, &ca) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_design_compile(&b, 512u, 512u, 30, 9u, &cb) == ODM_STATUS_OK);

        /* La MISMA evidencia musical entra en los dos. */
        memset(&comp, 0, sizeof(comp));
        memset(&dir, 0, sizeof(dir));
        comp.schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
        comp.radial_gain_q31 = (uint32_t)INT32_MAX;
        comp.radial_aperture_q31 = (uint32_t)INT32_MAX;
        comp.core_scale_q31 = 1150000000u;
        comp.flags = ODM_COMPOSITION_FLAG_RADIAL_HIRES | ODM_COMPOSITION_FLAG_STRICT_CAUSAL |
                     ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE | ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE;
        for (k = 0u; k < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++k) {
            uint32_t v = (uint32_t)(((uint64_t)(k + 1u) * 22369621u) % 2147483647u);
            comp.radial_q31[k] = v;
            comp.radial_body_q31[k] = v;
        }
        dir.schema_version = ODM_DIRECTOR_SCHEMA_VERSION;
        dir.layout = ODM_DIRECTOR_LAYOUT_MONOLITH;

        ODM_TEST_CHECK(context,
            odm_layered_resolve_frame_plan(&ca, &comp, &dir, 4800, 48000, &pa) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context,
            odm_layered_resolve_frame_plan(&cb, &comp, &dir, 4800, 48000, &pb) == ODM_STATUS_OK);

        /* Los valores que vienen de la musica son identicos. */
        for (k = 0u; k < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++k) {
            ODM_TEST_CHECK(context, pa.radial_q31[k] == pb.radial_q31[k]);
            ODM_TEST_CHECK(context, pa.radial_body_q31[k] == pb.radial_body_q31[k]);
            ODM_TEST_CHECK(context, pa.radial_release_q31[k] == pb.radial_release_q31[k]);
            ODM_TEST_CHECK(context, pa.radial_attack_q31[k] == pb.radial_attack_q31[k]);
        }
        ODM_TEST_CHECK(context, pa.sample == pb.sample);
        ODM_TEST_CHECK(context, pa.tick_index == pb.tick_index);
        ODM_TEST_CHECK(context, pa.progress_q31 == pb.progress_q31);
        ODM_TEST_CHECK(context, pa.core_gain_q31 == pb.core_gain_q31);
        ODM_TEST_CHECK(context, pa.duration_samples == pb.duration_samples);

        /* Y control negativo: la APARIENCIA si tiene que haber cambiado. Si no,
         * el test estaria comparando dos cosas iguales y no probaria nada. */
        ODM_TEST_CHECK(context, ca.background.style != cb.background.style);
        ODM_TEST_CHECK(context, ca.field.bar_shape != cb.field.bar_shape);
        ODM_TEST_CHECK(context, ca.field.bar_max_q16 != cb.field.bar_max_q16 ||
                                ca.field.bar_width_q16 != cb.field.bar_width_q16);
    }

    /* --- EL MANIFIESTO DESCRIBE TODO LO QUE HAY ----------------------------
     *
     * Si el manifiesto se quedara corto, quien lo lea concluiria que el motor
     * hace menos de lo que hace -- que es exactamente el problema que existe
     * para resolver. Se comprueba que TODA clave publicada aparece en el. */
    {
        uint64_t need = 0u;
        char *json;
        ODM_TEST_CHECK(context,
            odm_design_manifest_json(NULL, 0u, &need) == ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, need > 1000u);
        json = (char *)malloc((size_t)need);
        ODM_TEST_CHECK(context, json != NULL);
        if (json != NULL) {
            uint32_t k;
            ODM_TEST_CHECK(context,
                odm_design_manifest_json(json, need, &need) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, json[0] == '{');
            ODM_TEST_CHECK(context, json[need - 2u] == '}');
            for (k = 0u; k < n; ++k) {
                odm_design_control c;
                ODM_TEST_CHECK(context, odm_design_control_at(k, &c) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context, strstr(json, c.key) != NULL);
                ODM_TEST_CHECK(context, strstr(json, c.label) != NULL);
            }
            for (k = 0u; k < nt; ++k)
                ODM_TEST_CHECK(context, strstr(json, odm_template_name(k)) != NULL);
            free(json);
        }
        /* Un buffer justo por debajo debe fallar, no truncar en silencio. */
        {
            char corto[64];
            uint64_t req = 0u;
            ODM_TEST_CHECK(context,
                odm_design_manifest_json(corto, sizeof(corto), &req) == ODM_STATUS_BUFFER_TOO_SMALL);
        }
    }

    /* --- IDENTIDAD DE POLITICA --------------------------------------------- */
    {
        uint8_t bytes[2048];
        uint64_t required = 0u;
        ODM_TEST_CHECK(context, odm_design_policy_bytes(NULL, 0u, &required) == ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, required == 2048u);
        ODM_TEST_CHECK(context, odm_design_policy_bytes(bytes, sizeof(bytes), &required) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(bytes, "ODMDSGN1", 8u) == 0);
    }
}
