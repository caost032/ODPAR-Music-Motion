#include "renderer_internal.h"

#include <limits.h>

/* Frozen sRGB EOTF table, 8-bit code -> linear-light Q4.27.
 * Generated once from IEC 61966-2-1 transfer equations using 80-digit Decimal
 * arithmetic and nearest ties away from zero. It is renderer-versioned truth. */
static const int32_t odm_srgb8_to_linear_q27[256] = {
    0, 40739, 81477, 122216, 162955, 203694, 244432, 285171,
    325910, 366648, 407387, 449164, 493452, 540188, 589409, 641152,
    695451, 752343, 811861, 874038, 938908, 1006503, 1076855, 1149994,
    1225953, 1304760, 1386445, 1471039, 1558569, 1649065, 1742553, 1839062,
    1938620, 2041252, 2146986, 2255848, 2367863, 2483058, 2601456, 2723085,
    2847967, 2976128, 3107592, 3242383, 3380523, 3522037, 3666948, 3815278,
    3967051, 4122289, 4281013, 4443247, 4609011, 4778328, 4951218, 5127704,
    5307806, 5491545, 5678941, 5870016, 6064790, 6263282, 6465513, 6671502,
    6881270, 7094836, 7312219, 7533438, 7758514, 7987463, 8220306, 8457061,
    8697747, 8942382, 9190984, 9443572, 9700164, 9960777, 10225429, 10494138,
    10766922, 11043798, 11324783, 11609895, 11899151, 12192568, 12490163, 12791952,
    13097952, 13408180, 13722653, 14041387, 14364398, 14691702, 15023316, 15359256,
    15699537, 16044176, 16393189, 16746590, 17104396, 17466622, 17833284, 18204397,
    18579977, 18960038, 19344596, 19733666, 20127262, 20525401, 20928096, 21335362,
    21747215, 22163668, 22584737, 23010435, 23440778, 23875779, 24315453, 24759815,
    25208877, 25662655, 26121163, 26584414, 27052422, 27525202, 28002767, 28485130,
    28972306, 29464308, 29961149, 30462844, 30969405, 31480846, 31997181, 32518422,
    33044583, 33575677, 34111717, 34652716, 35198687, 35749644, 36305599, 36866565,
    37432554, 38003581, 38579656, 39160794, 39747006, 40338305, 40934704, 41536216,
    42142852, 42754625, 43371547, 43993632, 44620890, 45253334, 45890977, 46533831,
    47181907, 47835218, 48493775, 49157591, 49826678, 50501047, 51180711, 51865681,
    52555968, 53251586, 53952544, 54658856, 55370532, 56087585, 56810025, 57537864,
    58271115, 59009787, 59753893, 60503444, 61258451, 62018925, 62784878, 63556322,
    64333266, 65115723, 65903703, 66697218, 67496278, 68300895, 69111079, 69926842,
    70748195, 71575147, 72407711, 73245897, 74089716, 74939179, 75794295, 76655077,
    77521534, 78393678, 79271519, 80155067, 81044334, 81939328, 82840063, 83746546,
    84658790, 85576804, 86500599, 87430185, 88365572, 89306771, 90253793, 91206646,
    92165342, 93129891, 94100303, 95076588, 96058756, 97046817, 98040781, 99040659,
    100046459, 101058194, 102075871, 103099502, 104129095, 105164662, 106206212, 107253754,
    108307299, 109366856, 110432435, 111504045, 112581698, 113665401, 114755165, 115851000,
    116952915, 118060920, 119175024, 120295236, 121421567, 122554026, 123692623, 124837366,
    125988266, 127145331, 128308572, 129477996, 130653615, 131835437, 133023472, 134217728
};

static uint64_t mag_i64(int64_t v) {
    if (v >= 0) return (uint64_t)v;
    return (uint64_t)(-(v + INT64_C(1))) + UINT64_C(1);
}

static int64_t div_round_away(int64_t numerator, int64_t denominator) {
    int64_t q = numerator / denominator;
    int64_t r = numerator % denominator;
    uint64_t rm = mag_i64(r), dm = mag_i64(denominator);
    if (rm >= (dm - rm)) q += ((numerator < 0) != (denominator < 0)) ? -1 : 1;
    return q;
}

odm_render_q4_27 odm_renderer_decode_u8(uint8_t value, uint32_t transfer) {
    if (transfer == ODM_COLOR_TRANSFER_SRGB) return odm_srgb8_to_linear_q27[value];
    if (transfer == ODM_COLOR_TRANSFER_LINEAR) {
        return (odm_render_q4_27)div_round_away((int64_t)value * ODM_RENDER_Q27_ONE, 255);
    }
    return 0;
}

odm_q1_31 odm_renderer_alpha_u8(uint8_t value) {
    return (odm_q1_31)div_round_away((int64_t)value * INT32_MAX, 255);
}

uint16_t odm_renderer_q27_to_u16(odm_render_q4_27 value) {
    int64_t v = value;
    if (v < 0) v = 0;
    if (v > ODM_RENDER_Q27_ONE) v = ODM_RENDER_Q27_ONE;
    return (uint16_t)div_round_away(v * INT64_C(65535), ODM_RENDER_Q27_ONE);
}

uint16_t odm_renderer_q31_to_u16(odm_q1_31 value) {
    int64_t v = value;
    if (v < 0) v = 0;
    if (v > INT32_MAX) v = INT32_MAX;
    return (uint16_t)div_round_away(v * INT64_C(65535), INT32_MAX);
}

uint8_t odm_renderer_linear_u16_to_srgb8(uint16_t value) {
    int64_t target = div_round_away((int64_t)value * ODM_RENDER_Q27_ONE, 65535);
    uint32_t low = 0u, high = 255u;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2u;
        if ((int64_t)odm_srgb8_to_linear_q27[mid] < target) low = mid + 1u;
        else high = mid;
    }
    if (low == 0u) return 0u;
    {
        int64_t upper = (int64_t)odm_srgb8_to_linear_q27[low] - target;
        int64_t lower = target - (int64_t)odm_srgb8_to_linear_q27[low - 1u];
        return (uint8_t)(lower < upper ? low - 1u : low);
    }
}

odm_status odm_renderer_q27_mul_q31(odm_render_q4_27 value, odm_q1_31 factor,
                                    odm_render_q4_27 *out) {
    int64_t product, rounded;
    if (!out || factor < 0) return ODM_STATUS_INVALID_ARGUMENT;
    product = (int64_t)value * (int64_t)factor;
    rounded = div_round_away(product, INT32_MAX);
    if (rounded > INT32_MAX || rounded < INT32_MIN) return ODM_STATUS_OVERFLOW;
    *out = (odm_render_q4_27)rounded;
    return ODM_STATUS_OK;
}

odm_status odm_renderer_q31_lerp(odm_q1_31 a, odm_q1_31 b, odm_q1_31 t,
                                 odm_q1_31 *out) {
    int64_t left, right, total;
    if (!out || a < 0 || b < 0 || t < 0) return ODM_STATUS_INVALID_ARGUMENT;
    left = (int64_t)a * (int64_t)(INT32_MAX - t);
    right = (int64_t)b * (int64_t)t;
    total = div_round_away(left + right, INT32_MAX);
    if (total > INT32_MAX) total = INT32_MAX;
    *out = (odm_q1_31)total;
    return ODM_STATUS_OK;
}

odm_status odm_renderer_q27_lerp(odm_render_q4_27 a, odm_render_q4_27 b,
                                 odm_q1_31 t, odm_render_q4_27 *out) {
    int64_t left, right, total;
    if (!out || t < 0) return ODM_STATUS_INVALID_ARGUMENT;
    left = (int64_t)a * (int64_t)(INT32_MAX - t);
    right = (int64_t)b * (int64_t)t;
    total = div_round_away(left + right, INT32_MAX);
    if (total > INT32_MAX || total < INT32_MIN) return ODM_STATUS_OVERFLOW;
    *out = (odm_render_q4_27)total;
    return ODM_STATUS_OK;
}

void odm_renderer_blend_over(odm_linear_pixel *dst, const odm_linear_pixel *src) {
    odm_q1_31 inv;
    odm_render_q4_27 dr, dg, db;
    int64_t alpha;
    if (!dst || !src) return;
    inv = (odm_q1_31)(INT32_MAX - src->a);
    if (odm_renderer_q27_mul_q31(dst->r, inv, &dr) != ODM_STATUS_OK ||
        odm_renderer_q27_mul_q31(dst->g, inv, &dg) != ODM_STATUS_OK ||
        odm_renderer_q27_mul_q31(dst->b, inv, &db) != ODM_STATUS_OK) return;
    dst->r = src->r + dr; if (dst->r > ODM_RENDER_Q27_ONE) dst->r = ODM_RENDER_Q27_ONE;
    dst->g = src->g + dg; if (dst->g > ODM_RENDER_Q27_ONE) dst->g = ODM_RENDER_Q27_ONE;
    dst->b = src->b + db; if (dst->b > ODM_RENDER_Q27_ONE) dst->b = ODM_RENDER_Q27_ONE;
    alpha = (int64_t)src->a + ((int64_t)dst->a * inv + INT32_MAX / 2) / INT32_MAX;
    if (alpha > INT32_MAX) alpha = INT32_MAX;
    dst->a = (odm_q1_31)alpha;
}
