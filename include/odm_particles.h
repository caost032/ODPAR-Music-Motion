#ifndef ODM_PARTICLES_H
#define ODM_PARTICLES_H

/* Simulacion de particulas: independientes, con inercia y colision.
 *
 * QUE LO SEPARA DE LO ANTERIOR
 *
 * Antes cada particula tenia un sitio estable y la musica la desplazaba desde
 * el. Era reactivo, pero no estaba vivo: sin velocidad no hay inercia, y sin
 * inercia un empujon no deja huella -- la particula vuelve a su sitio en cuanto
 * el golpe pasa. Aqui cada particula lleva su propia velocidad, la conserva, y
 * un choque le cambia la direccion PARA SIEMPRE, como en el aire de verdad.
 *
 * LA COLISION ES LO QUE LO HACE REAL
 *
 * El nucleo tiene radio, y ese radio crece con los graves medidos. Cuando
 * crece, alcanza particulas que antes no tocaba y las empuja hacia fuera por su
 * normal, con impulso proporcional a CUANTO ha crecido en este paso. No es un
 * empujon radial generico aplicado a todo el campo: es un choque, y solo lo
 * sienten las que el nucleo alcanza.
 *
 * LA NATURALEZA ES EL AIRE
 *
 * Cada naturaleza es un campo de fuerzas declarado -- viento, caida, ascenso,
 * flotacion, turbulencia -- que arrastra el campo entero. Es lo que hace que
 * las particulas entren por un lado y salgan por otro en vez de quedarse
 * suspendidas como piedras. Una pluma y una mota de polvo se distinguen sobre
 * todo por su arrastre y su peso, no por su dibujo.
 *
 * TIEMPO
 *
 * La integracion ocurre sobre la rejilla de 100 Hz del analisis, NUNCA sobre
 * cuadros. Por eso la misma cancion produce el mismo movimiento a 24, 30 o 60
 * fps: los FPS eligen cuando se mira, no como se mueve. Entre dos pasos, la
 * presentacion interpola linealmente, igual que hace el instrumento espectral.
 *
 * Todo es entero y con semilla: sin coma flotante, sin reloj de pared, sin
 * `Math.random()`. Dos renders del mismo proyecto dan el mismo aire.
 */

#include "odm_status.h"

#include <stdint.h>

#define ODM_PARTICLES_SCHEMA_VERSION UINT32_C(1)
#define ODM_PARTICLES_MAX            UINT32_C(256)
#define ODM_PARTICLES_POLICY_BYTES   UINT32_C(256)

/* Naturaleza: el campo de fuerzas que arrastra el aire. */
enum {
    ODM_PARTICLE_NATURE_FLOAT      = 0u, /* suspension casi neutra            */
    ODM_PARTICLE_NATURE_WIND_RIGHT = 1u, /* arrastre lateral hacia la derecha */
    ODM_PARTICLE_NATURE_WIND_LEFT  = 2u,
    ODM_PARTICLE_NATURE_FALL       = 3u, /* caida con deriva                  */
    ODM_PARTICLE_NATURE_RISE       = 4u, /* ascenso, tipo brasa o ceniza      */
    ODM_PARTICLE_NATURE_SWIRL      = 5u, /* remolino alrededor del centro     */
    ODM_PARTICLE_NATURE_COUNT      = 6u
};

typedef struct {
    uint32_t schema_version;
    uint32_t count;
    uint32_t nature;
    uint32_t flow_q31;       /* fuerza del arrastre                          */
    uint32_t drag_q31;       /* rozamiento: cuanto retiene la velocidad      */
    uint32_t impulse_q31;    /* que tan fuerte empuja el nucleo al chocar    */
    /* Aleteo: oscilacion transversal al movimiento, con fase propia por
     * particula. Es lo que separa una pluma de una mota: las dos van con el
     * viento, pero la pluma se contonea al ir. A cero, el campo es polvo. */
    uint32_t flutter_q31;
    uint32_t reserved;
    uint64_t seed;
} odm_particles_config;

typedef struct {
    uint32_t schema_version;
    uint32_t count;
    uint32_t width;          /* lienzo en pixeles, para el reciclado         */
    uint32_t height;
    uint64_t tick_index;
    int32_t x_q16[ODM_PARTICLES_MAX];
    int32_t y_q16[ODM_PARTICLES_MAX];
    int32_t vx_q16[ODM_PARTICLES_MAX];
    int32_t vy_q16[ODM_PARTICLES_MAX];
} odm_particles_state;

/* Coloca el campo. La disposicion sale de la semilla y es estable. */
odm_status odm_particles_init(const odm_particles_config *config,
                              uint32_t width, uint32_t height,
                              odm_particles_state *out_state);

/* Un paso de 10 ms: fuerzas, integracion, colision con el nucleo y reciclado.
 *
 * `core_radius_q16` es el radio del nucleo EN ESTE paso y `core_grow_q16` lo
 * que ha crecido respecto al anterior. El impulso del choque es proporcional al
 * crecimiento: un nucleo quieto toca las particulas pero no las lanza. */
odm_status odm_particles_step(odm_particles_state *state,
                              const odm_particles_config *config,
                              int32_t center_x_q16, int32_t center_y_q16,
                              int32_t core_radius_q16, int32_t core_grow_q16);

odm_status odm_particles_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                      uint64_t *out_required);

#endif /* ODM_PARTICLES_H */
