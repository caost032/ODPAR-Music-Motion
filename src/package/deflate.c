#include "package_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define ODM_DEFLATE_HASH_SLOTS UINT32_C(65536)
#define ODM_DEFLATE_MAX_INPUT UINT64_C(536870912)

static uint32_t crc_table_entry(uint32_t x) {
    uint32_t c = x;
    uint32_t i;
    for (i = 0u; i < 8u; ++i) {
        uint32_t mask = (uint32_t)(0u - (c & 1u));
        c = (c >> 1u) ^ (UINT32_C(0xedb88320) & mask);
    }
    return c;
}

static void crc_table_build(uint32_t table[256]) {
    uint32_t i;
    for (i = 0u; i < 256u; ++i) table[i] = crc_table_entry(i);
}

odm_status odm_pkg_crc32(const uint8_t *data, uint64_t bytes, uint32_t *out_crc32) {
    uint32_t table[256];
    uint32_t crc = UINT32_C(0xffffffff);
    uint64_t i;
    if (out_crc32 == NULL || (data == NULL && bytes != 0u)) return ODM_STATUS_INVALID_ARGUMENT;
    crc_table_build(table);
    for (i = 0u; i < bytes; ++i) crc = table[(crc ^ data[i]) & UINT32_C(0xff)] ^ (crc >> 8u);
    *out_crc32 = crc ^ UINT32_C(0xffffffff);
    return ODM_STATUS_OK;
}

typedef struct {
    uint8_t *out;
    uint64_t capacity;
    uint64_t bytes;
    uint64_t bit_buffer;
    uint32_t bit_count;
    odm_status status;
} bit_writer;

static void bw_emit_byte(bit_writer *w, uint8_t b) {
    if (w->status != ODM_STATUS_OK) return;
    if (w->out != NULL) {
        if (w->bytes >= w->capacity) { w->status = ODM_STATUS_BUFFER_TOO_SMALL; return; }
        w->out[w->bytes] = b;
    }
    if (w->bytes == UINT64_MAX) { w->status = ODM_STATUS_OVERFLOW; return; }
    w->bytes++;
}

static void bw_bits(bit_writer *w, uint32_t value, uint32_t count) {
    if (w->status != ODM_STATUS_OK || count > 24u) { if (count > 24u) w->status = ODM_STATUS_INTERNAL_ERROR; return; }
    w->bit_buffer |= ((uint64_t)value & ((UINT64_C(1) << count) - UINT64_C(1))) << w->bit_count;
    w->bit_count += count;
    while (w->bit_count >= 8u) {
        bw_emit_byte(w, (uint8_t)(w->bit_buffer & UINT64_C(0xff)));
        w->bit_buffer >>= 8u;
        w->bit_count -= 8u;
    }
}

static void bw_finish(bit_writer *w) {
    if (w->status == ODM_STATUS_OK && w->bit_count != 0u) {
        bw_emit_byte(w, (uint8_t)(w->bit_buffer & UINT64_C(0xff)));
        w->bit_buffer = 0u;
        w->bit_count = 0u;
    }
}

static uint32_t reverse_bits(uint32_t v, uint32_t n) {
    uint32_t r = 0u;
    uint32_t i;
    for (i = 0u; i < n; ++i) { r = (r << 1u) | (v & 1u); v >>= 1u; }
    return r;
}

static void emit_fixed_symbol(bit_writer *w, uint32_t sym) {
    uint32_t code, bits;
    if (sym <= 143u) { code = UINT32_C(0x30) + sym; bits = 8u; }
    else if (sym <= 255u) { code = UINT32_C(0x190) + (sym - 144u); bits = 9u; }
    else if (sym <= 279u) { code = sym - 256u; bits = 7u; }
    else { code = UINT32_C(0xc0) + (sym - 280u); bits = 8u; }
    bw_bits(w, reverse_bits(code, bits), bits);
}

static const uint16_t LEN_BASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t LEN_EXTRA[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const uint16_t DIST_BASE[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t DIST_EXTRA[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static void emit_match(bit_writer *w, uint32_t length, uint32_t distance) {
    uint32_t i;
    for (i = 0u; i < 29u; ++i) {
        uint32_t maxv = (uint32_t)LEN_BASE[i] + ((UINT32_C(1) << LEN_EXTRA[i]) - 1u);
        if (i == 28u) maxv = 258u;
        if (length <= maxv) {
            emit_fixed_symbol(w, 257u + i);
            if (LEN_EXTRA[i] != 0u) bw_bits(w, length - LEN_BASE[i], LEN_EXTRA[i]);
            break;
        }
    }
    for (i = 0u; i < 30u; ++i) {
        uint32_t maxd = (uint32_t)DIST_BASE[i] + ((UINT32_C(1) << DIST_EXTRA[i]) - 1u);
        if (distance <= maxd) {
            bw_bits(w, reverse_bits(i, 5u), 5u);
            if (DIST_EXTRA[i] != 0u) bw_bits(w, distance - DIST_BASE[i], DIST_EXTRA[i]);
            break;
        }
    }
}

static uint32_t hash3(const uint8_t *p) {
    uint32_t x = (uint32_t)p[0] * UINT32_C(251) + (uint32_t)p[1] * UINT32_C(31) + (uint32_t)p[2];
    x ^= x >> 8u;
    return x & UINT32_C(0xffff);
}

odm_status odm_pkg_deflate_fixed(const uint8_t *data, uint64_t bytes,
                                 uint8_t *out, uint64_t capacity,
                                 uint64_t *out_required) {
    int32_t *last;
    bit_writer w;
    uint64_t pos = 0u;
    if (out_required == NULL || (data == NULL && bytes != 0u)) return ODM_STATUS_INVALID_ARGUMENT;
    if (bytes > ODM_DEFLATE_MAX_INPUT) return ODM_STATUS_BUDGET_EXCEEDED;
    last = (int32_t *)malloc((size_t)ODM_DEFLATE_HASH_SLOTS * sizeof(*last));
    if (last == NULL) return ODM_STATUS_OUT_OF_MEMORY;
    memset(last, 0xff, (size_t)ODM_DEFLATE_HASH_SLOTS * sizeof(*last));
    w.out = out; w.capacity = capacity; w.bytes = 0u; w.bit_buffer = 0u; w.bit_count = 0u; w.status = ODM_STATUS_OK;
    bw_bits(&w, 1u, 1u); /* BFINAL */
    bw_bits(&w, 1u, 2u); /* BTYPE=01 fixed Huffman, LSB-first */
    while (pos < bytes && w.status == ODM_STATUS_OK) {
        uint32_t best_len = 0u, best_dist = 0u;
        if (pos + 2u < bytes) {
            uint32_t h = hash3(data + pos);
            int32_t prev = last[h];
            last[h] = (int32_t)pos;
            if (prev >= 0 && pos >= (uint64_t)(uint32_t)prev) {
                uint64_t dist64 = pos - (uint64_t)(uint32_t)prev;
                if (dist64 >= 1u && dist64 <= ODM_DEFLATE_WINDOW) {
                    uint64_t max64 = bytes - pos;
                    uint32_t max = max64 > 258u ? 258u : (uint32_t)max64;
                    uint32_t n = 0u;
                    while (n < max && data[(uint64_t)(uint32_t)prev + n] == data[pos + n]) ++n;
                    if (n >= 3u) { best_len = n; best_dist = (uint32_t)dist64; }
                }
            }
        }
        if (best_len >= 3u) {
            uint32_t j;
            emit_match(&w, best_len, best_dist);
            for (j = 1u; j < best_len; ++j) {
                uint64_t q = pos + j;
                if (q + 2u < bytes) last[hash3(data + q)] = (int32_t)q;
            }
            pos += best_len;
        } else {
            emit_fixed_symbol(&w, data[pos]);
            pos++;
        }
    }
    emit_fixed_symbol(&w, 256u);
    bw_finish(&w);
    free(last);
    *out_required = w.bytes;
    if (w.status == ODM_STATUS_BUFFER_TOO_SMALL && out == NULL) return ODM_STATUS_OK;
    return w.status;
}

typedef struct {
    const uint8_t *data;
    uint64_t size;
    uint64_t pos;
    uint64_t bit_buffer;
    uint32_t bit_count;
} bit_reader;

static int br_bits(bit_reader *r, uint32_t count, uint32_t *out) {
    uint64_t mask;
    while (r->bit_count < count) {
        if (r->pos >= r->size) return 0;
        r->bit_buffer |= (uint64_t)r->data[r->pos++] << r->bit_count;
        r->bit_count += 8u;
    }
    mask = (UINT64_C(1) << count) - UINT64_C(1);
    *out = (uint32_t)(r->bit_buffer & mask);
    r->bit_buffer >>= count;
    r->bit_count -= count;
    return 1;
}

static int decode_fixed_symbol(bit_reader *r, uint32_t *out_sym) {
    uint32_t code = 0u, len;
    for (len = 1u; len <= 9u; ++len) {
        uint32_t b, rev, sym;
        if (!br_bits(r, 1u, &b)) return 0;
        code |= b << (len - 1u);
        rev = reverse_bits(code, len);
        if (len == 7u && rev <= 23u) { *out_sym = 256u + rev; return 1; }
        if (len == 8u) {
            if (rev >= UINT32_C(0x30) && rev <= UINT32_C(0xbf)) { sym = rev - UINT32_C(0x30); *out_sym = sym; return 1; }
            if (rev >= UINT32_C(0xc0) && rev <= UINT32_C(0xc7)) { *out_sym = 280u + (rev - UINT32_C(0xc0)); return 1; }
        }
        if (len == 9u && rev >= UINT32_C(0x190) && rev <= UINT32_C(0x1ff)) { *out_sym = 144u + (rev - UINT32_C(0x190)); return 1; }
    }
    return 0;
}

static uint32_t crc_step(const uint32_t table[256], uint32_t crc, uint8_t b) {
    return table[(crc ^ b) & UINT32_C(0xff)] ^ (crc >> 8u);
}

odm_status odm_pkg_inflate_fixed(const uint8_t *data, uint64_t bytes,
                                 uint8_t *out, uint64_t capacity,
                                 uint64_t expected_bytes,
                                 uint64_t *out_required,
                                 uint32_t *out_crc32,
                                 odm_sha256_digest *out_sha256) {
    bit_reader r;
    uint8_t window[ODM_DEFLATE_WINDOW];
    uint32_t table[256];
    uint32_t crc = UINT32_C(0xffffffff);
    uint64_t produced = 0u;
    odm_sha256_context hash = ODM_SHA256_CONTEXT_INITIALIZER;
    uint32_t final_bit, type;
    odm_status st;
    if (data == NULL || out_required == NULL || out_crc32 == NULL || out_sha256 == NULL) return ODM_STATUS_INVALID_ARGUMENT;
    if (out == NULL && capacity != 0u) return ODM_STATUS_INVALID_ARGUMENT;
    if (expected_bytes > ODM_DEFLATE_MAX_INPUT) return ODM_STATUS_BUDGET_EXCEEDED;
    crc_table_build(table);
    st = odm_sha256_init(&hash); if (st != ODM_STATUS_OK) return st;
    r.data = data; r.size = bytes; r.pos = 0u; r.bit_buffer = 0u; r.bit_count = 0u;
    if (!br_bits(&r, 1u, &final_bit) || !br_bits(&r, 2u, &type)) return ODM_STATUS_INVALID_DATA;
    if (final_bit != 1u || type != 1u) return ODM_STATUS_UNSUPPORTED;
    for (;;) {
        uint32_t sym;
        if (!decode_fixed_symbol(&r, &sym)) return ODM_STATUS_INVALID_DATA;
        if (sym < 256u) {
            uint8_t b = (uint8_t)sym;
            if (produced >= expected_bytes) return ODM_STATUS_INVALID_DATA;
            window[produced & (ODM_DEFLATE_WINDOW - 1u)] = b;
            if (out != NULL && produced < capacity) out[produced] = b;
            crc = crc_step(table, crc, b);
            st = odm_sha256_update(&hash, &b, 1u); if (st != ODM_STATUS_OK) return st;
            produced++;
        } else if (sym == 256u) {
            break;
        } else if (sym <= 285u) {
            uint32_t li = sym - 257u, extra = 0u, length, dcode, dist, j;
            if (li >= 29u) return ODM_STATUS_INVALID_DATA;
            if (LEN_EXTRA[li] != 0u && !br_bits(&r, LEN_EXTRA[li], &extra)) return ODM_STATUS_INVALID_DATA;
            length = (uint32_t)LEN_BASE[li] + extra;
            if (!br_bits(&r, 5u, &dcode)) return ODM_STATUS_INVALID_DATA;
            dcode = reverse_bits(dcode, 5u);
            if (dcode >= 30u) return ODM_STATUS_INVALID_DATA;
            extra = 0u;
            if (DIST_EXTRA[dcode] != 0u && !br_bits(&r, DIST_EXTRA[dcode], &extra)) return ODM_STATUS_INVALID_DATA;
            dist = (uint32_t)DIST_BASE[dcode] + extra;
            if ((uint64_t)dist > produced || produced + length > expected_bytes) return ODM_STATUS_INVALID_DATA;
            for (j = 0u; j < length; ++j) {
                uint8_t b = window[(produced - dist) & (ODM_DEFLATE_WINDOW - 1u)];
                window[produced & (ODM_DEFLATE_WINDOW - 1u)] = b;
                if (out != NULL && produced < capacity) out[produced] = b;
                crc = crc_step(table, crc, b);
                st = odm_sha256_update(&hash, &b, 1u); if (st != ODM_STATUS_OK) return st;
                produced++;
            }
        } else return ODM_STATUS_INVALID_DATA;
    }
    if (produced != expected_bytes) return ODM_STATUS_INVALID_DATA;
    /* Canonical profile requires all remaining bits in the final byte to be zero
     * and no trailing bytes after that byte. */
    if (r.bit_buffer != 0u || r.pos != bytes) return ODM_STATUS_INVALID_DATA;
    if (out != NULL && capacity < produced) return ODM_STATUS_BUFFER_TOO_SMALL;
    st = odm_sha256_final(&hash, out_sha256); if (st != ODM_STATUS_OK) return st;
    *out_required = produced;
    *out_crc32 = crc ^ UINT32_C(0xffffffff);
    return ODM_STATUS_OK;
}
