#ifndef ODPAR_LAB_SCENE_H
#define ODPAR_LAB_SCENE_H
#include <stdint.h>
#include <stddef.h>
int odpar_lab_render_scene(uint8_t *rgba, uint32_t width, uint32_t height,
                           double yaw_deg, double pitch_deg, double distance_m,
                           double phase, int shadows, uint64_t *out_fragments,
                           char *error, size_t error_cap);
#endif
