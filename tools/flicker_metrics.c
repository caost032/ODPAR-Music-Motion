/* Medidor de defectos temporales sobre una secuencia de Composition Frame State.
 *
 * Convierte "esa linea parpadea" en un numero comparable. Sin esto, cualquier
 * cambio visual se decide mirando un video, que es exactamente como se llega a
 * un motor que parece correcto a velocidad normal y falla a 0.25x.
 *
 * Metricas, todas por objetivo visual y sobre la linea de tiempo de muestras:
 *
 *   ON_OFF        veces que el valor cruza de cero a no-cero o al reves.
 *                 Es el sintoma directo de "aparece y desaparece".
 *   ONE_FRAME     veces que el valor esta activo exactamente un fotograma.
 *                 Es el parpadeo que solo se ve en camara lenta.
 *   REVERSALS     cambios de signo de la derivada. Mide agitacion: un valor
 *                 que sube y baja sin descanso se siente nervioso aunque no
 *                 llegue a apagarse.
 *   MAX_JUMP      mayor salto entre fotogramas consecutivos, en Q1.31.
 *                 Un salto grande es un teletransporte visual.
 *   MEAN_ABS_D    magnitud media del cambio por fotograma.
 *
 * No hay umbral "bueno" cableado: la herramienta compara dos secuencias y
 * reporta. La interpretacion es del que lee.
 */

#include "odm_visual.h"
#include "odm_visual_scene.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t on_off;
    uint64_t one_frame;
    uint64_t reversals;
    uint32_t max_jump;
    uint64_t sum_abs_delta;
    uint64_t samples;
} metric;

static void metric_run(const uint32_t *series, uint64_t n, metric *m) {
    uint64_t i;
    int previous_dir = 0;
    memset(m, 0, sizeof(*m));
    m->samples = n;
    for (i = 1u; i < n; ++i) {
        uint32_t a = series[i - 1u], b = series[i];
        uint32_t d = a > b ? a - b : b - a;
        int dir = b > a ? 1 : (b < a ? -1 : 0);
        m->sum_abs_delta += d;
        if (d > m->max_jump) m->max_jump = d;
        if ((a == 0u) != (b == 0u)) m->on_off++;
        if (dir != 0 && previous_dir != 0 && dir != previous_dir) m->reversals++;
        if (dir != 0) previous_dir = dir;
    }
    for (i = 1u; i + 1u < n; ++i) {
        if (series[i] != 0u && series[i - 1u] == 0u && series[i + 1u] == 0u)
            m->one_frame++;
    }
}

static void metric_print(const char *name, const metric *m) {
    double mean = m->samples > 1u
        ? (double)m->sum_abs_delta / (double)(m->samples - 1u) : 0.0;
    printf("  %-16s on_off=%-6llu one_frame=%-5llu reversals=%-6llu "
           "max_jump=%-10u mean_abs_delta=%.1f\n",
           name,
           (unsigned long long)m->on_off,
           (unsigned long long)m->one_frame,
           (unsigned long long)m->reversals,
           m->max_jump, mean);
}

/* Extrae una serie temporal de un campo del frame state. */
#define SERIES(field) \
    do { \
        uint64_t k; \
        for (k = 0u; k < n; ++k) series[k] = frames[k].field; \
    } while (0)

static void analyse(const char *label, const odm_composition_frame_state *frames,
                    uint64_t n, uint32_t *series) {
    metric m;
    printf("%s (%llu fotogramas)\n", label, (unsigned long long)n);

    SERIES(core_breath_q31);  metric_run(series, n, &m); metric_print("core_breath", &m);
    SERIES(core_border_q31);  metric_run(series, n, &m); metric_print("core_border", &m);
    SERIES(halo_q31);         metric_run(series, n, &m); metric_print("halo", &m);
    SERIES(grid_q31);         metric_run(series, n, &m); metric_print("grid", &m);
    SERIES(particles_q31);    metric_run(series, n, &m); metric_print("particles", &m);
    SERIES(memory_echo_q31);  metric_run(series, n, &m); metric_print("memory_echo", &m);
    SERIES(accent_q31);       metric_run(series, n, &m); metric_print("accent", &m);
    SERIES(chroma_split_q31); metric_run(series, n, &m); metric_print("chroma_split", &m);

    /* Los 96 radiales se agregan: lo que interesa es cuantos parpadeos hay en
     * total en el halo, no cual lane concreto lo hizo. */
    {
        metric total;
        uint32_t lane;
        memset(&total, 0, sizeof(total));
        total.samples = n;
        for (lane = 0u; lane < (uint32_t)ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++lane) {
            uint64_t k;
            metric one;
            for (k = 0u; k < n; ++k) series[k] = frames[k].radial_attack_q31[lane];
            metric_run(series, n, &one);
            total.on_off += one.on_off;
            total.one_frame += one.one_frame;
            total.reversals += one.reversals;
            total.sum_abs_delta += one.sum_abs_delta;
            if (one.max_jump > total.max_jump) total.max_jump = one.max_jump;
        }
        metric_print("radial_attack*96", &total);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    FILE *fh;
    odm_composition_frame_state *raw = NULL, *dyn = NULL;
    uint32_t *series = NULL;
    uint64_t n = 0u, i;
    odm_visual_scene scene;
    int64_t samples_per_frame;
    int32_t fps = 60;

    if (argc < 2) {
        fprintf(stderr, "uso: %s <frames.bin> [fps]\n"
                        "  frames.bin: volcado crudo de odm_composition_frame_state\n", argv[0]);
        return 2;
    }
    if (argc >= 3) fps = (int32_t)atoi(argv[2]);
    if (fps <= 0) { fprintf(stderr, "fps invalido\n"); return 2; }
    samples_per_frame = (int64_t)48000 / (int64_t)fps;

    fh = fopen(argv[1], "rb");
    if (!fh) { fprintf(stderr, "no se pudo abrir %s\n", argv[1]); return 3; }
    fseek(fh, 0, SEEK_END);
    {
        long bytes = ftell(fh);
        if (bytes <= 0 || (size_t)bytes % sizeof(*raw) != 0u) {
            fprintf(stderr, "tamano no multiplo de frame state\n"); fclose(fh); return 4;
        }
        n = (uint64_t)bytes / sizeof(*raw);
    }
    fseek(fh, 0, SEEK_SET);
    raw = (odm_composition_frame_state *)malloc((size_t)n * sizeof(*raw));
    dyn = (odm_composition_frame_state *)malloc((size_t)n * sizeof(*dyn));
    series = (uint32_t *)malloc((size_t)n * sizeof(*series));
    if (!raw || !dyn || !series) { fprintf(stderr, "oom\n"); fclose(fh); return 5; }
    if (fread(raw, sizeof(*raw), (size_t)n, fh) != (size_t)n) {
        fprintf(stderr, "lectura incompleta\n"); fclose(fh); return 6;
    }
    fclose(fh);

    /* Misma entrada, dos caminos: sin dinamica y con dinamica. */
    if (odm_visual_scene_init(&scene, 0u) != ODM_STATUS_OK) {
        fprintf(stderr, "scene init fallo\n"); return 7;
    }
    for (i = 0u; i < n; ++i) {
        uint64_t sample = (uint64_t)((int64_t)i * samples_per_frame);
        if (odm_visual_scene_apply(&scene, &raw[i], sample, &dyn[i]) != ODM_STATUS_OK) {
            fprintf(stderr, "scene apply fallo en %llu\n", (unsigned long long)i); return 8;
        }
    }

    printf("=== METRICAS TEMPORALES @ %d fps ===\n\n", fps);
    analyse("SIN Visual Dynamics", raw, n, series);
    analyse("CON Visual Dynamics", dyn, n, series);

    free(raw); free(dyn); free(series);
    return 0;
}
