/* Simulacion de particulas. Contrato y justificacion en odm_particles.h.
 *
 * Enteros en todo el recorrido. Las posiciones y velocidades viven en Q16.16
 * pixeles; la velocidad se expresa POR PASO de 10 ms, de modo que el paso de
 * integracion es 1 y no aparece ningun factor de tiempo suelto que pudiera
 * discrepar entre plataformas.
 */

#include "odm_particles.h"

#include "odm_wire.h"

#include <limits.h>
#include <string.h>

#define PA_Q ((int64_t)INT32_MAX)

static uint64_t pa_mix64(uint64_t x) {
    x ^= x >> 33; x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33; x *= UINT64_C(0xc4ceb9fe1a85ec53);
    x ^= x >> 33;
    return x;
}

static int32_t pa_clamp_i32(int64_t v) {
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int32_t)v;
}

/* Raiz entera exacta, la misma familia que usa el resto del motor. */
static uint64_t pa_isqrt(uint64_t x) {
    uint64_t res = 0u, bit = UINT64_C(1) << 62;
    while (bit > x) bit >>= 2;
    while (bit != 0u) {
        if (x >= res + bit) { x -= res + bit; res = (res >> 1) + bit; }
        else res >>= 1;
        bit >>= 2;
    }
    return res;
}

/* Onda suave por tramos cuadraticos, periodo 2^32, salida en Q31.
 *
 * No es un seno y no pretende serlo: es el aleteo de una pluma. Lo que tiene
 * que cumplir es ser continua, periodica, entera y reproducible -- y eso lo
 * cumple exactamente. Un CORDIC aqui costaria 31 iteraciones por particula y
 * por paso sin cambiar nada de lo que el ojo ve. */
static int32_t pa_wave_q31(uint32_t phase) {
    int64_t x = (int64_t)phase - INT64_C(0x80000000);
    int64_t a = x < 0 ? -x : x;
    int64_t y = (x * (INT64_C(0x80000000) - a)) / (INT64_C(1) << 29);
    if (y > INT32_MAX) y = INT32_MAX;
    if (y < -INT32_MAX) y = -INT32_MAX;
    return (int32_t)y;
}

odm_status odm_particles_init(const odm_particles_config *config,
                              uint32_t width, uint32_t height,
                              odm_particles_state *out_state) {
    uint32_t i;
    if (!config || !out_state || width == 0u || height == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    if (config->schema_version != ODM_PARTICLES_SCHEMA_VERSION) return ODM_STATUS_VERSION_MISMATCH;
    if (config->count > ODM_PARTICLES_MAX) return ODM_STATUS_INVALID_ARGUMENT;
    if (config->nature >= ODM_PARTICLE_NATURE_COUNT) return ODM_STATUS_INVALID_ARGUMENT;
    if (config->flow_q31 > (uint32_t)INT32_MAX || config->drag_q31 > (uint32_t)INT32_MAX ||
        config->impulse_q31 > (uint32_t)INT32_MAX ||
        config->flutter_q31 > (uint32_t)INT32_MAX ||
        config->reserved != 0u) return ODM_STATUS_INVALID_ARGUMENT;

    memset(out_state, 0, sizeof(*out_state));
    out_state->schema_version = ODM_PARTICLES_SCHEMA_VERSION;
    out_state->count = config->count;
    out_state->width = width;
    out_state->height = height;
    for (i = 0u; i < config->count; ++i) {
        uint64_t h = pa_mix64(config->seed ^ ((uint64_t)i * UINT64_C(0x9e3779b97f4a7c15)));
        uint64_t h2 = pa_mix64(h);
        out_state->x_q16[i] = (int32_t)(((h & UINT64_C(0xffffffff)) * (uint64_t)width) >> 16);
        out_state->y_q16[i] = (int32_t)(((h2 & UINT64_C(0xffffffff)) * (uint64_t)height) >> 16);
        /* Velocidad inicial pequena y dispersa: el aire ya esta en movimiento
         * cuando empieza el video, no arranca de golpe. */
        out_state->vx_q16[i] = (int32_t)((int64_t)((h >> 32) & UINT64_C(0xffff)) - 32768) / 512;
        out_state->vy_q16[i] = (int32_t)((int64_t)((h2 >> 32) & UINT64_C(0xffff)) - 32768) / 512;
    }
    return ODM_STATUS_OK;
}

/* Fuerza de la naturaleza para una particula, en Q16.16 por paso. */
static void pa_nature_force(uint32_t nature, uint32_t flow_q31, uint64_t h,
                            int32_t x_q16, int32_t y_q16,
                            int32_t cx_q16, int32_t cy_q16,
                            int32_t *out_fx, int32_t *out_fy) {
    /* El caudal se expresa en pixeles por paso: a 100 pasos por segundo, un
     * caudal de 1.0 mueve unos 6 pixeles por segundo por unidad. */
    int64_t f = ((int64_t)flow_q31 * 4096) / PA_Q;
    /* Dispersion por particula: sin ella el campo entero se mueve en bloque y
     * se lee como una textura desplazada, no como aire. */
    int64_t j = (int64_t)((h >> 40) & UINT64_C(0xff)) - 128;
    int64_t vx = 0, vy = 0;
    switch (nature) {
        case ODM_PARTICLE_NATURE_WIND_RIGHT: vx = f;            vy = (f * j) / 512; break;
        case ODM_PARTICLE_NATURE_WIND_LEFT:  vx = -f;           vy = (f * j) / 512; break;
        case ODM_PARTICLE_NATURE_FALL:       vy = f;            vx = (f * j) / 384; break;
        case ODM_PARTICLE_NATURE_RISE:       vy = -f;           vx = (f * j) / 384; break;
        case ODM_PARTICLE_NATURE_SWIRL: {
            /* Remolino: fuerza perpendicular al radio. Girar alrededor del
             * centro es lo unico que hace que el aire parezca tener un ojo. */
            int64_t dx = (int64_t)x_q16 - cx_q16, dy = (int64_t)y_q16 - cy_q16;
            int64_t d = (int64_t)pa_isqrt((uint64_t)((dx >> 8) * (dx >> 8) +
                                                     (dy >> 8) * (dy >> 8))) << 8;
            if (d > 0) { vx = (-dy * f) / d; vy = (dx * f) / d; }
            break;
        }
        default:
            /* Flotacion: deriva suave y distinta para cada una. */
            vx = (f * j) / 256;
            vy = (f * (((int64_t)((h >> 48) & UINT64_C(0xff))) - 128)) / 256;
            break;
    }
    *out_fx = pa_clamp_i32(vx);
    *out_fy = pa_clamp_i32(vy);
}

odm_status odm_particles_step(odm_particles_state *state,
                              const odm_particles_config *config,
                              int32_t center_x_q16, int32_t center_y_q16,
                              int32_t core_radius_q16, int32_t core_grow_q16) {
    uint32_t i;
    int64_t w_q16, h_q16;
    if (!state || !config) return ODM_STATUS_INVALID_ARGUMENT;
    if (state->schema_version != ODM_PARTICLES_SCHEMA_VERSION) return ODM_STATUS_VERSION_MISMATCH;
    if (state->count > ODM_PARTICLES_MAX || state->count != config->count)
        return ODM_STATUS_INVALID_ARGUMENT;
    if (config->nature >= ODM_PARTICLE_NATURE_COUNT) return ODM_STATUS_INVALID_ARGUMENT;

    w_q16 = (int64_t)state->width << 16;
    h_q16 = (int64_t)state->height << 16;
    if (core_grow_q16 < 0) core_grow_q16 = 0;

    for (i = 0u; i < state->count; ++i) {
        uint64_t h = pa_mix64(config->seed ^ ((uint64_t)i * UINT64_C(0x9e3779b97f4a7c15)));
        int32_t fx = 0, fy = 0;
        int64_t vx, vy, x, y, dx, dy, d;

        pa_nature_force(config->nature, config->flow_q31, h,
                        state->x_q16[i], state->y_q16[i],
                        center_x_q16, center_y_q16, &fx, &fy);

        /* Rozamiento: la velocidad se conserva en parte y tiende al caudal.
         * Es lo que da inercia -- un empujon sigue notandose despues -- sin
         * dejar que la velocidad crezca sin limite. */
        vx = ((int64_t)state->vx_q16[i] * (int64_t)config->drag_q31) / PA_Q + fx;
        vy = ((int64_t)state->vy_q16[i] * (int64_t)config->drag_q31) / PA_Q + fy;

        /* ALETEO.
         *
         * Empujon perpendicular al movimiento, con fase y ritmo propios de cada
         * particula. Una pluma arrastrada por el viento no viaja recta: se
         * contonea. Sin esto, cambiar el dibujo de mota a pluma solo cambia la
         * silueta -- la pluma seguiria siendo una piedra con forma de pluma. */
        if (config->flutter_q31 != 0u) {
            uint32_t paso = UINT32_C(0x02000000) +
                            (uint32_t)((h >> 24) & UINT64_C(0x01ffffff));
            uint32_t fase = (uint32_t)(h >> 16) +
                            (uint32_t)(state->tick_index * (uint64_t)paso);
            int64_t onda = pa_wave_q31(fase);
            int64_t amp = ((int64_t)config->flutter_q31 * 3072) / PA_Q;
            int64_t px, py, m;
            /* Perpendicular a la velocidad; si esta parada, al eje horizontal. */
            m = (int64_t)pa_isqrt((uint64_t)((vx >> 8) * (vx >> 8) +
                                             (vy >> 8) * (vy >> 8))) << 8;
            if (m > 0) { px = (-vy * PA_Q) / m; py = (vx * PA_Q) / m; }
            else { px = 0; py = PA_Q; }
            vx += (px * ((onda * amp) / PA_Q)) / PA_Q;
            vy += (py * ((onda * amp) / PA_Q)) / PA_Q;
        }

        x = (int64_t)state->x_q16[i] + vx;
        y = (int64_t)state->y_q16[i] + vy;

        /* COLISION CON EL NUCLEO.
         *
         * Si la particula queda dentro del radio, se la expulsa a la
         * superficie por su normal y recibe impulso proporcional a cuanto ha
         * crecido el nucleo en este paso. Un nucleo quieto la sostiene fuera;
         * un nucleo que crece la lanza. */
        dx = x - center_x_q16;
        dy = y - center_y_q16;
        d = (int64_t)pa_isqrt((uint64_t)((dx >> 8) * (dx >> 8) + (dy >> 8) * (dy >> 8))) << 8;
        if (core_radius_q16 > 0 && d < (int64_t)core_radius_q16) {
            int64_t nx, ny, kick;
            if (d <= 0) { nx = PA_Q; ny = 0; d = 1; }
            else { nx = (dx * PA_Q) / d; ny = (dy * PA_Q) / d; }
            /* A la superficie: nunca se dibuja dentro del nucleo. */
            x = (int64_t)center_x_q16 + (nx * core_radius_q16) / PA_Q;
            y = (int64_t)center_y_q16 + (ny * core_radius_q16) / PA_Q;
            /* Impulso del choque, que persiste por la inercia. */
            kick = ((int64_t)core_grow_q16 * (int64_t)config->impulse_q31) / PA_Q;
            vx = (nx * kick) / PA_Q;
            vy = (ny * kick) / PA_Q;
        }

        /* Reciclado: lo que sale por un lado entra por el otro. El aire no se
         * agota, que es justo lo que distingue un campo vivo de una tanda de
         * particulas que se acaba. */
        while (x < 0) x += w_q16;
        while (x >= w_q16) x -= w_q16;
        while (y < 0) y += h_q16;
        while (y >= h_q16) y -= h_q16;

        state->x_q16[i] = pa_clamp_i32(x);
        state->y_q16[i] = pa_clamp_i32(y);
        state->vx_q16[i] = pa_clamp_i32(vx);
        state->vy_q16[i] = pa_clamp_i32(vy);
    }
    state->tick_index++;
    return ODM_STATUS_OK;
}

odm_status odm_particles_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                      uint64_t *out_required) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    odm_status st;
    uint64_t usados = 0u;
    const uint64_t bytes = ODM_PARTICLES_POLICY_BYTES;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = bytes;
    if (!buffer || capacity < bytes) return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, (size_t)bytes);
    st = odm_wire_writer_init(&w, buffer, bytes);
    if (st != ODM_STATUS_OK) return st;
#define PP(x) do { st = (x); if (st != ODM_STATUS_OK) return st; } while (0)
    PP(odm_wire_write_bytes(&w, "ODMPART1", 8u));
    PP(odm_wire_write_u32(&w, ODM_PARTICLES_SCHEMA_VERSION));
    PP(odm_wire_write_u32(&w, ODM_PARTICLES_MAX));
    PP(odm_wire_write_u32(&w, ODM_PARTICLE_NATURE_COUNT));
    PP(odm_wire_write_u32(&w, 100u)); /* pasos por segundo: la rejilla de analisis */
    PP(odm_wire_write_u32(&w, 1u));   /* la integracion es por tick, no por cuadro */
    PP(odm_wire_write_u32(&w, 1u));   /* el impulso del choque es proporcional al crecimiento */
    PP(odm_wire_write_u32(&w, 1u));   /* ninguna particula se dibuja dentro del nucleo */
    PP(odm_wire_write_u32(&w, 1u));   /* el campo se recicla: lo que sale vuelve a entrar */
    PP(odm_wire_write_u32(&w, 1u));   /* enteros y semilla: sin coma flotante ni reloj */
    PP(odm_wire_write_u32(&w, 3072u)); /* amplitud del aleteo por unidad, Q16 por paso */
    PP(odm_wire_write_u32(&w, 4096u)); /* caudal por unidad, Q16 por paso              */
    PP(odm_wire_write_u32(&w, 1u));   /* el aleteo es perpendicular a la velocidad     */
#undef PP
    /* El registro tiene tamano FIJO: lo que el escritor gasta es cuanto ocupa
     * el contenido, no cuanto vale la identidad. Publicar lo gastado haria que
     * anadir una constante cambiara el tamano declarado del registro. */
    st = odm_wire_writer_finish(&w, &usados);
    if (st != ODM_STATUS_OK) return st;
    if (usados > bytes) return ODM_STATUS_OVERFLOW;
    return ODM_STATUS_OK;
}
