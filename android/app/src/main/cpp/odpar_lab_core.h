#ifndef ODPAR_LAB_CORE_H
#define ODPAR_LAB_CORE_H
#include <stddef.h>
#include <stdint.h>
int odpar_lab_status(char *out, size_t cap);
int odpar_lab_selftest(char *out, size_t cap);
int odpar_lab_spine_summary(char *out, size_t cap);
int odpar_lab_spine_full(char *out, size_t cap);
int odpar_lab_render_demo(uint32_t width, uint32_t height,
                          int32_t pan_x_mm, int32_t pan_y_mm, int32_t zoom_mm,
                          uint8_t *rgba, uint64_t rgba_bytes,
                          char *meta, size_t meta_cap);
#endif
