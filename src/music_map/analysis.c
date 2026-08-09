#include "odm_music_map.h"
#include "odm_fixed.h"
#include "odm_wire.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define MUSIC_CORDIC_K_Q31 INT64_C(1304065748)
#define MUSIC_QUARTER_PHASE UINT32_C(0x40000000)
#define MUSIC_POLICY_MAGIC "ODMPOL1\0"
#define MUSIC_POLICY_ROUNDING_ID 1u
#define MUSIC_POLICY_WINDOW_ID 1u
#define MUSIC_POLICY_TRANSFORM_ID 1u
#define MUSIC_POLICY_FEATURE_ID 1u
#define MUSIC_POLICY_RESAMPLE_ID 1u

static const uint32_t music_cordic_atan_phase[31] = {
    536870912u, 316933406u, 167458907u, 85004756u, 42667331u,
    21354465u, 10679838u, 5340245u, 2670163u, 1335087u, 667544u,
    333772u, 166886u, 83443u, 41722u, 20861u, 10430u, 5215u, 2608u,
    1304u, 652u, 326u, 163u, 81u, 41u, 20u, 10u, 5u, 3u, 1u, 1u
};

static uint64_t music_i64_mag(int64_t value) {
    if (value >= 0) return (uint64_t)value;
    return (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1);
}

static int64_t music_div_pow2_trunc(int64_t value, unsigned shift) {
    int64_t divisor = INT64_C(1) << shift;
    return value / divisor;
}

static int32_t music_sin_q31(uint32_t phase) {
    uint32_t quadrant = phase >> 30;
    uint32_t offset = phase & UINT32_C(0x3fffffff);
    int sign = quadrant >= 2u ? -1 : 1;
    int64_t z = (quadrant == 0u || quadrant == 2u)
                    ? (int64_t)offset
                    : (int64_t)(MUSIC_QUARTER_PHASE - offset);
    int64_t x = MUSIC_CORDIC_K_Q31;
    int64_t y = INT64_C(0);
    unsigned index;

    for (index = 0u; index < 31u; index++) {
        int64_t x_shift = music_div_pow2_trunc(x, index);
        int64_t y_shift = music_div_pow2_trunc(y, index);
        int64_t next_x;
        int64_t next_y;
        if (z >= 0) {
            next_x = x - y_shift;
            next_y = y + x_shift;
            z -= (int64_t)music_cordic_atan_phase[index];
        } else {
            next_x = x + y_shift;
            next_y = y - x_shift;
            z += (int64_t)music_cordic_atan_phase[index];
        }
        x = next_x;
        y = next_y;
    }
    if (sign < 0) y = -y;
    if (y > INT32_MAX) y = INT32_MAX;
    if (y < INT32_MIN) y = INT32_MIN;
    return (int32_t)y;
}

static int32_t music_cos_q31(uint32_t phase) {
    return music_sin_q31(phase + MUSIC_QUARTER_PHASE);
}

static int32_t music_round_half_i64(int64_t value) {
    uint64_t magnitude = music_i64_mag(value);
    uint64_t rounded = magnitude / UINT64_C(2) +
                       ((magnitude & UINT64_C(1)) != 0u ? UINT64_C(1) : UINT64_C(0));
    if (value < 0) {
        if (rounded >= (UINT64_C(1) << 31)) return INT32_MIN;
        return -(int32_t)rounded;
    }
    if (rounded > (uint64_t)INT32_MAX) return INT32_MAX;
    return (int32_t)rounded;
}

static uint64_t music_square_q31(int32_t value) {
    uint64_t magnitude = music_i64_mag((int64_t)value);
    return magnitude * magnitude;
}

static uint32_t music_energy_sample_q31(int32_t value) {
    uint64_t square = music_square_q31(value);
    uint64_t encoded = (square >> 31) +
                       (((square & UINT64_C(0x7fffffff)) >= UINT64_C(0x40000000))
                            ? UINT64_C(1)
                            : UINT64_C(0));
    if (encoded > UINT32_MAX) encoded = UINT32_MAX;
    return (uint32_t)encoded;
}

static uint64_t music_isqrt_u64(uint64_t value) {
    uint64_t result = UINT64_C(0);
    uint64_t bit = UINT64_C(1) << 62;
    while (bit > value) bit >>= 2;
    while (bit != 0u) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static int32_t music_rms_q31(uint64_t sum_energy_q31, uint32_t count) {
    uint64_t mean;
    uint64_t radicand;
    uint64_t root;
    if (count == 0u) return 0;
    mean = (sum_energy_q31 + (uint64_t)(count / 2u)) / (uint64_t)count;
    if (mean > (UINT64_MAX >> 31)) return INT32_MAX;
    radicand = mean << 31;
    root = music_isqrt_u64(radicand);
    if (root > (uint64_t)INT32_MAX) root = (uint64_t)INT32_MAX;
    return (int32_t)root;
}

static uint32_t music_magnitude_q31(int32_t real, int32_t imag) {
    uint64_t total = music_square_q31(real) + music_square_q31(imag);
    uint64_t energy = (total >> 31) +
                      (((total & UINT64_C(0x7fffffff)) >= UINT64_C(0x40000000))
                           ? UINT64_C(1)
                           : UINT64_C(0));
    uint64_t radicand;
    uint64_t root;
    if (energy > UINT32_MAX) energy = UINT32_MAX;
    radicand = energy << 31;
    root = music_isqrt_u64(radicand);
    if (root > UINT32_MAX) root = UINT32_MAX;
    return (uint32_t)root;
}

static uint32_t music_energy_complex_q31(int32_t real, int32_t imag) {
    uint64_t total = music_square_q31(real) + music_square_q31(imag);
    uint64_t energy = (total >> 31) +
                      (((total & UINT64_C(0x7fffffff)) >= UINT64_C(0x40000000))
                           ? UINT64_C(1)
                           : UINT64_C(0));
    if (energy > UINT32_MAX) energy = UINT32_MAX;
    return (uint32_t)energy;
}

static int32_t music_q31_mul(int32_t left, int32_t right) {
    odm_q1_31 out = 0;
    if (odm_q1_31_mul(left, right, &out) != ODM_STATUS_OK) return 0;
    return out;
}

static int32_t music_clamp_i64_q31(int64_t value) {
    if (value > INT32_MAX) return INT32_MAX;
    if (value < INT32_MIN) return INT32_MIN;
    return (int32_t)value;
}

static void music_fft_bit_reverse(odm_music_complex_q31 *values) {
    uint32_t i;
    uint32_t j = 0u;
    for (i = 1u; i < ODM_MUSIC_WINDOW_SAMPLES; i++) {
        uint32_t bit = ODM_MUSIC_WINDOW_SAMPLES >> 1;
        while ((j & bit) != 0u) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            odm_music_complex_q31 temporary = values[i];
            values[i] = values[j];
            values[j] = temporary;
        }
    }
}

static void music_fft(odm_music_complex_q31 *values,
                      const odm_music_complex_q31 *twiddles) {
    uint32_t length;
    music_fft_bit_reverse(values);
    for (length = 2u; length <= ODM_MUSIC_WINDOW_SAMPLES; length <<= 1) {
        uint32_t half = length >> 1;
        uint32_t stride = ODM_MUSIC_WINDOW_SAMPLES / length;
        uint32_t base;
        for (base = 0u; base < ODM_MUSIC_WINDOW_SAMPLES; base += length) {
            uint32_t j;
            for (j = 0u; j < half; j++) {
                const odm_music_complex_q31 w = twiddles[j * stride];
                odm_music_complex_q31 even = values[base + j];
                odm_music_complex_q31 odd = values[base + j + half];
                int32_t ac = music_q31_mul(odd.real, w.real);
                int32_t bd = music_q31_mul(odd.imag, w.imag);
                int32_t ad = music_q31_mul(odd.real, w.imag);
                int32_t bc = music_q31_mul(odd.imag, w.real);
                int64_t rotated_real = (int64_t)ac - (int64_t)bd;
                int64_t rotated_imag = (int64_t)ad + (int64_t)bc;
                int64_t top_real = (int64_t)even.real + rotated_real;
                int64_t top_imag = (int64_t)even.imag + rotated_imag;
                int64_t bottom_real = (int64_t)even.real - rotated_real;
                int64_t bottom_imag = (int64_t)even.imag - rotated_imag;
                values[base + j].real = music_round_half_i64(top_real);
                values[base + j].imag = music_round_half_i64(top_imag);
                values[base + j + half].real = music_round_half_i64(bottom_real);
                values[base + j + half].imag = music_round_half_i64(bottom_imag);
            }
        }
        if (length == ODM_MUSIC_WINDOW_SAMPLES) break;
    }
}

static odm_status music_hash_i32_le(const int32_t *values, uint64_t count,
                                    odm_sha256_digest *out_digest) {
    odm_sha256_context context = ODM_SHA256_CONTEXT_INITIALIZER;
    uint64_t index;
    odm_status status;
    if ((!values && count != 0u) || !out_digest) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_sha256_init(&context);
    if (status != ODM_STATUS_OK) return status;
    for (index = 0u; index < count; index++) {
        uint32_t value = (uint32_t)values[index];
        uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                            (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
        status = odm_sha256_update(&context, bytes, sizeof(bytes));
        if (status != ODM_STATUS_OK) return status;
    }
    return odm_sha256_final(&context, out_digest);
}

static odm_status music_hash_complex_le(const odm_music_complex_q31 *values,
                                        uint64_t count,
                                        odm_sha256_digest *out_digest) {
    odm_sha256_context context = ODM_SHA256_CONTEXT_INITIALIZER;
    uint64_t index;
    odm_status status;
    if ((!values && count != 0u) || !out_digest) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_sha256_init(&context);
    if (status != ODM_STATUS_OK) return status;
    for (index = 0u; index < count; index++) {
        uint32_t parts[2] = {(uint32_t)values[index].real, (uint32_t)values[index].imag};
        unsigned part;
        for (part = 0u; part < 2u; part++) {
            uint32_t value = parts[part];
            uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                                (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
            status = odm_sha256_update(&context, bytes, sizeof(bytes));
            if (status != ODM_STATUS_OK) return status;
        }
    }
    return odm_sha256_final(&context, out_digest);
}

static odm_status music_policy_encode(const odm_music_analysis_plan *plan,
                                      uint8_t *buffer, uint64_t capacity,
                                      uint64_t *out_required) {
    odm_wire_writer writer = ODM_WIRE_WRITER_INITIALIZER;
    uint32_t index;
    uint64_t written = 0u;
    odm_status status;

    if (!plan || !out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = ODM_MUSIC_POLICY_BYTES;
    if (!buffer || capacity < ODM_MUSIC_POLICY_BYTES) return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, ODM_MUSIC_POLICY_BYTES);
    status = odm_wire_writer_init(&writer, buffer, ODM_MUSIC_POLICY_BYTES);
    if (status != ODM_STATUS_OK) return status;
#define MUSIC_WRITE(call) do { status = (call); if (status != ODM_STATUS_OK) return status; } while (0)
    MUSIC_WRITE(odm_wire_write_bytes(&writer, MUSIC_POLICY_MAGIC, 8u));
    MUSIC_WRITE(odm_wire_write_u32(&writer, ODM_MUSIC_POLICY_VERSION));
    MUSIC_WRITE(odm_wire_write_u32(&writer, plan->sample_rate));
    MUSIC_WRITE(odm_wire_write_u32(&writer, plan->tick_rate));
    MUSIC_WRITE(odm_wire_write_u32(&writer, plan->tick_samples));
    MUSIC_WRITE(odm_wire_write_u32(&writer, plan->window_samples));
    MUSIC_WRITE(odm_wire_write_u32(&writer, plan->fft_bins));
    MUSIC_WRITE(odm_wire_write_u32(&writer, MUSIC_POLICY_ROUNDING_ID));
    MUSIC_WRITE(odm_wire_write_u32(&writer, MUSIC_POLICY_WINDOW_ID));
    MUSIC_WRITE(odm_wire_write_u32(&writer, MUSIC_POLICY_TRANSFORM_ID));
    MUSIC_WRITE(odm_wire_write_u32(&writer, MUSIC_POLICY_FEATURE_ID));
    MUSIC_WRITE(odm_wire_write_u32(&writer, MUSIC_POLICY_RESAMPLE_ID));
    MUSIC_WRITE(odm_wire_write_u32(&writer, 1024u));
    MUSIC_WRITE(odm_wire_write_u32(&writer, 64u));
    MUSIC_WRITE(odm_wire_write_u32(&writer, 512u));
    MUSIC_WRITE(odm_wire_write_u32(&writer, 30u));
    MUSIC_WRITE(odm_wire_write_u32(&writer, 23u));
    MUSIC_WRITE(odm_wire_write_u32(&writer, 25u));
    MUSIC_WRITE(odm_wire_write_u32(&writer, ODM_MUSIC_BAND_COUNT));
    for (index = 0u; index < ODM_MUSIC_BAND_COUNT + 1u; index++) {
        MUSIC_WRITE(odm_wire_write_u32(&writer, plan->band_bin_edges[index]));
    }
    for (index = 0u; index < 3u; index++) {
        MUSIC_WRITE(odm_wire_write_u32(&writer, plan->envelope_divisors[index]));
    }
    MUSIC_WRITE(odm_wire_write_u32(&writer, plan->silence_rms_threshold_q31));
    MUSIC_WRITE(odm_wire_write_bytes(&writer, plan->window_sha256.bytes,
                                     ODM_SHA256_DIGEST_BYTES));
    MUSIC_WRITE(odm_wire_write_bytes(&writer, plan->twiddle_sha256.bytes,
                                     ODM_SHA256_DIGEST_BYTES));
    MUSIC_WRITE(odm_wire_write_u32(&writer, 0u));
    MUSIC_WRITE(odm_wire_writer_finish(&writer, &written));
#undef MUSIC_WRITE
    if (written > ODM_MUSIC_POLICY_BYTES) return ODM_STATUS_INVARIANT_BROKEN;
    /* Remaining bytes are already canonical zero reserved space. */
    return ODM_STATUS_OK;
}

odm_status odm_music_policy_bytes(const odm_music_analysis_plan *plan,
                                  uint8_t *buffer, uint64_t capacity,
                                  uint64_t *out_required) {
    return music_policy_encode(plan, buffer, capacity, out_required);
}

odm_status odm_music_analysis_plan_build(
    int32_t *window_q31, uint64_t window_capacity,
    odm_music_complex_q31 *twiddles_q31, uint64_t twiddle_capacity,
    odm_music_analysis_plan *out_plan) {
    odm_music_analysis_plan plan;
    uint32_t index;
    uint8_t policy[ODM_MUSIC_POLICY_BYTES];
    uint64_t policy_required = 0u;
    odm_status status;

    if (!window_q31 || !twiddles_q31 || !out_plan) return ODM_STATUS_INVALID_ARGUMENT;
    if (window_capacity < ODM_MUSIC_WINDOW_SAMPLES ||
        twiddle_capacity < ODM_MUSIC_FFT_TWIDDLES) {
        return ODM_STATUS_BUFFER_TOO_SMALL;
    }
    memset(&plan, 0, sizeof(plan));
    plan.version = ODM_MUSIC_ANALYSIS_VERSION;
    plan.sample_rate = ODM_MUSIC_SAMPLE_RATE;
    plan.tick_rate = ODM_MUSIC_TICK_RATE;
    plan.tick_samples = ODM_MUSIC_TICK_SAMPLES;
    plan.window_samples = ODM_MUSIC_WINDOW_SAMPLES;
    plan.fft_bins = ODM_MUSIC_FFT_BINS;
    plan.feature_schema = 1u;
    plan.band_bin_edges[0] = 0u;
    plan.band_bin_edges[1] = 6u;
    plan.band_bin_edges[2] = 11u;
    plan.band_bin_edges[3] = 22u;
    plan.band_bin_edges[4] = 86u;
    plan.band_bin_edges[5] = 342u;
    plan.band_bin_edges[6] = ODM_MUSIC_FFT_BINS;
    plan.envelope_divisors[0] = 4u;
    plan.envelope_divisors[1] = 16u;
    plan.envelope_divisors[2] = 64u;
    plan.silence_rms_threshold_q31 = UINT32_C(2097152); /* exactly 1/1024 FS */

    for (index = 0u; index < ODM_MUSIC_WINDOW_SAMPLES; index++) {
        uint32_t phase = index << 21; /* 2^32 / 2048 */
        int32_t cosine = music_cos_q31(phase);
        int64_t numerator = (int64_t)INT32_MAX - (int64_t)cosine;
        window_q31[index] = music_round_half_i64(numerator);
    }
    for (index = 0u; index < ODM_MUSIC_FFT_TWIDDLES; index++) {
        uint32_t phase = index << 21;
        twiddles_q31[index].real = music_cos_q31(phase);
        twiddles_q31[index].imag = music_sin_q31((uint32_t)(0u - phase));
    }

    status = music_hash_i32_le(window_q31, ODM_MUSIC_WINDOW_SAMPLES,
                               &plan.window_sha256);
    if (status != ODM_STATUS_OK) return status;
    status = music_hash_complex_le(twiddles_q31, ODM_MUSIC_FFT_TWIDDLES,
                                   &plan.twiddle_sha256);
    if (status != ODM_STATUS_OK) return status;
    status = music_policy_encode(&plan, policy, sizeof(policy), &policy_required);
    if (status != ODM_STATUS_OK) return status;
    if (policy_required != sizeof(policy)) return ODM_STATUS_INVARIANT_BROKEN;
    status = odm_sha256(policy, sizeof(policy), &plan.policy_sha256);
    if (status != ODM_STATUS_OK) return status;
    *out_plan = plan;
    return ODM_STATUS_OK;
}

odm_status odm_music_analysis_plan_validate(
    const odm_music_analysis_plan *plan, const int32_t *window_q31,
    uint64_t window_count, const odm_music_complex_q31 *twiddles_q31,
    uint64_t twiddle_count) {
    odm_sha256_digest window_hash;
    odm_sha256_digest twiddle_hash;
    odm_sha256_digest policy_hash;
    uint8_t policy[ODM_MUSIC_POLICY_BYTES];
    uint64_t required = 0u;
    static const uint32_t edges[ODM_MUSIC_BAND_COUNT + 1u] =
        {0u, 6u, 11u, 22u, 86u, 342u, ODM_MUSIC_FFT_BINS};
    uint32_t index;
    odm_status status;

    if (!plan || !window_q31 || !twiddles_q31) return ODM_STATUS_INVALID_ARGUMENT;
    if (plan->version != ODM_MUSIC_ANALYSIS_VERSION || plan->reserved != 0u ||
        plan->reserved2 != 0u || plan->sample_rate != ODM_MUSIC_SAMPLE_RATE ||
        plan->tick_rate != ODM_MUSIC_TICK_RATE ||
        plan->tick_samples != ODM_MUSIC_TICK_SAMPLES ||
        plan->window_samples != ODM_MUSIC_WINDOW_SAMPLES ||
        plan->fft_bins != ODM_MUSIC_FFT_BINS || plan->feature_schema != 1u ||
        window_count != ODM_MUSIC_WINDOW_SAMPLES ||
        twiddle_count != ODM_MUSIC_FFT_TWIDDLES) {
        return ODM_STATUS_VERSION_MISMATCH;
    }
    for (index = 0u; index < ODM_MUSIC_BAND_COUNT + 1u; index++) {
        if (plan->band_bin_edges[index] != edges[index]) return ODM_STATUS_INVARIANT_BROKEN;
    }
    if (plan->envelope_divisors[0] != 4u || plan->envelope_divisors[1] != 16u ||
        plan->envelope_divisors[2] != 64u ||
        plan->silence_rms_threshold_q31 != UINT32_C(2097152)) {
        return ODM_STATUS_INVARIANT_BROKEN;
    }
    status = music_hash_i32_le(window_q31, window_count, &window_hash);
    if (status != ODM_STATUS_OK) return status;
    status = music_hash_complex_le(twiddles_q31, twiddle_count, &twiddle_hash);
    if (status != ODM_STATUS_OK) return status;
    if (!odm_sha256_equal(&window_hash, &plan->window_sha256) ||
        !odm_sha256_equal(&twiddle_hash, &plan->twiddle_sha256)) {
        return ODM_STATUS_INTEGRITY_ERROR;
    }
    status = music_policy_encode(plan, policy, sizeof(policy), &required);
    if (status != ODM_STATUS_OK) return status;
    status = odm_sha256(policy, sizeof(policy), &policy_hash);
    if (status != ODM_STATUS_OK) return status;
    return odm_sha256_equal(&policy_hash, &plan->policy_sha256)
               ? ODM_STATUS_OK
               : ODM_STATUS_INTEGRITY_ERROR;
}

odm_status odm_music_policy_current_sha256(odm_sha256_digest *out_policy_sha256) {
    int32_t window_q31[ODM_MUSIC_WINDOW_SAMPLES];
    odm_music_complex_q31 twiddles_q31[ODM_MUSIC_FFT_TWIDDLES];
    odm_music_analysis_plan plan;
    odm_status status;
    if (!out_policy_sha256) return ODM_STATUS_INVALID_ARGUMENT;
    status = odm_music_analysis_plan_build(window_q31, ODM_MUSIC_WINDOW_SAMPLES,
                                           twiddles_q31, ODM_MUSIC_FFT_TWIDDLES,
                                           &plan);
    if (status != ODM_STATUS_OK) return status;
    *out_policy_sha256 = plan.policy_sha256;
    return ODM_STATUS_OK;
}

odm_status odm_music_tick_count(uint64_t canonical_frames,
                                uint64_t *out_tick_count) {
    uint64_t quotient;
    uint64_t remainder;
    if (!out_tick_count) return ODM_STATUS_INVALID_ARGUMENT;
    quotient = canonical_frames / ODM_MUSIC_TICK_SAMPLES;
    remainder = canonical_frames % ODM_MUSIC_TICK_SAMPLES;
    if (remainder != 0u) {
        if (quotient == UINT64_MAX) return ODM_STATUS_OVERFLOW;
        quotient++;
    }
    *out_tick_count = quotient;
    return ODM_STATUS_OK;
}

odm_status odm_music_analysis_state_init(odm_music_analysis_state *state) {
    if (!state) return ODM_STATUS_INVALID_ARGUMENT;
    memset(state, 0, sizeof(*state));
    return ODM_STATUS_OK;
}

static int32_t music_pcm_component(const odm_pcm_stereo_q31 *pcm,
                                   uint64_t frames, uint64_t index,
                                   unsigned component) {
    int64_t left;
    int64_t right;
    if (index >= frames) return 0;
    left = pcm[index].left;
    right = pcm[index].right;
    if (component == 0u) return (int32_t)left;
    if (component == 1u) return (int32_t)right;
    if (component == 2u) return music_clamp_i64_q31((left + right) / INT64_C(2));
    return music_clamp_i64_q31((left - right) / INT64_C(2));
}

static int music_window_absolute_index(uint64_t center_sample, int64_t relative,
                                       uint64_t *out_index) {
    uint64_t magnitude;
    if (!out_index) return 0;
    if (relative < 0) {
        magnitude = music_i64_mag(relative);
        if (center_sample < magnitude) return 0;
        *out_index = center_sample - magnitude;
        return 1;
    }
    magnitude = (uint64_t)relative;
    if (magnitude > UINT64_MAX - center_sample) return 0;
    *out_index = center_sample + magnitude;
    return 1;
}

static uint32_t music_average_u64(uint64_t sum, uint32_t count) {
    uint64_t value;
    if (count == 0u) return 0u;
    value = (sum + (uint64_t)(count / 2u)) / (uint64_t)count;
    if (value > UINT32_MAX) value = UINT32_MAX;
    return (uint32_t)value;
}

static uint32_t music_envelope_step(uint32_t previous, uint32_t current,
                                    uint32_t divisor) {
    int64_t delta = (int64_t)current - (int64_t)previous;
    int64_t adjustment = delta / (int64_t)divisor;
    int64_t remainder = delta % (int64_t)divisor;
    if (remainder != 0 &&
        music_i64_mag(remainder) >= (uint64_t)divisor - music_i64_mag(remainder)) {
        adjustment += delta < 0 ? -1 : 1;
    }
    {
        int64_t result = (int64_t)previous + adjustment;
        if (result < 0) return 0u;
        if ((uint64_t)result > UINT32_MAX) return UINT32_MAX;
        return (uint32_t)result;
    }
}

static int32_t music_ratio_q31(int64_t numerator, uint64_t denominator) {
    odm_q1_31 value = 0;
    if (denominator == 0u || denominator > (uint64_t)INT64_MAX) return 0;
    if (numerator > (int64_t)denominator) numerator = (int64_t)denominator;
    if (numerator < -(int64_t)denominator) numerator = -(int64_t)denominator;
    if (odm_q1_31_from_ratio(numerator, (int64_t)denominator, &value) != ODM_STATUS_OK) {
        return 0;
    }
    return value;
}

odm_status odm_music_analyze_tick(
    const odm_music_analysis_plan *plan, const int32_t *window_q31,
    const odm_music_complex_q31 *twiddles_q31,
    const odm_pcm_stereo_q31 *canonical_pcm, uint64_t canonical_frames,
    uint64_t tick_index, odm_music_analysis_state *state,
    odm_music_analysis_scratch *scratch, odm_music_analysis_tick *out_tick) {
    odm_music_analysis_tick result;
    uint64_t tick_count;
    uint64_t center_sample;
    uint64_t time_energy[4] = {0u, 0u, 0u, 0u};
    uint64_t band_sums[ODM_MUSIC_BAND_COUNT] = {0u, 0u, 0u, 0u, 0u, 0u};
    uint64_t total_spectrum = 0u;
    uint64_t flux_sum = 0u;
    uint64_t magnitude_sum = 0u;
    uint64_t weighted_bins = 0u;
    uint32_t index;
    odm_status status;

    if (!plan || !window_q31 || !twiddles_q31 || !state || !scratch || !out_tick ||
        (!canonical_pcm && canonical_frames != 0u)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = odm_music_analysis_plan_validate(plan, window_q31,
                                              ODM_MUSIC_WINDOW_SAMPLES,
                                              twiddles_q31,
                                              ODM_MUSIC_FFT_TWIDDLES);
    if (status != ODM_STATUS_OK) return status;
    status = odm_music_tick_count(canonical_frames, &tick_count);
    if (status != ODM_STATUS_OK) return status;
    if (tick_index >= tick_count) return ODM_STATUS_INVALID_ARGUMENT;
    if (tick_index > UINT64_MAX / ODM_MUSIC_TICK_SAMPLES) return ODM_STATUS_OVERFLOW;
    center_sample = tick_index * ODM_MUSIC_TICK_SAMPLES;
    memset(&result, 0, sizeof(result));
    result.tick_index = tick_index;
    result.center_sample = center_sample;

    for (index = 0u; index < ODM_MUSIC_WINDOW_SAMPLES; index++) {
        int64_t relative = (int64_t)index - INT64_C(1024);
        uint64_t absolute = 0u;
        int has_absolute;
        int32_t left;
        int32_t right;
        int32_t mid;
        int32_t side;
        int32_t windowed;
        has_absolute = music_window_absolute_index(center_sample, relative, &absolute);
        left = has_absolute != 0
                   ? music_pcm_component(canonical_pcm, canonical_frames, absolute, 0u)
                   : 0;
        right = has_absolute != 0
                    ? music_pcm_component(canonical_pcm, canonical_frames, absolute, 1u)
                    : 0;
        mid = has_absolute != 0
                  ? music_pcm_component(canonical_pcm, canonical_frames, absolute, 2u)
                  : 0;
        side = has_absolute != 0
                   ? music_pcm_component(canonical_pcm, canonical_frames, absolute, 3u)
                   : 0;
        time_energy[0] += music_energy_sample_q31(left);
        time_energy[1] += music_energy_sample_q31(right);
        time_energy[2] += music_energy_sample_q31(mid);
        time_energy[3] += music_energy_sample_q31(side);
        windowed = music_q31_mul(mid, window_q31[index]);
        scratch->fft[index].real = windowed;
        scratch->fft[index].imag = 0;
    }

    result.rms_left_q31 = music_rms_q31(time_energy[0], ODM_MUSIC_WINDOW_SAMPLES);
    result.rms_right_q31 = music_rms_q31(time_energy[1], ODM_MUSIC_WINDOW_SAMPLES);
    result.rms_mid_q31 = music_rms_q31(time_energy[2], ODM_MUSIC_WINDOW_SAMPLES);
    result.rms_side_q31 = music_rms_q31(time_energy[3], ODM_MUSIC_WINDOW_SAMPLES);

    music_fft(scratch->fft, twiddles_q31);
    for (index = 0u; index < ODM_MUSIC_FFT_BINS; index++) {
        uint32_t magnitude = music_magnitude_q31(scratch->fft[index].real,
                                                 scratch->fft[index].imag);
        uint32_t energy = music_energy_complex_q31(scratch->fft[index].real,
                                                    scratch->fft[index].imag);
        uint32_t band;
        scratch->magnitude_q31[index] = magnitude;
        total_spectrum += energy;
        magnitude_sum += magnitude;
        weighted_bins += (uint64_t)magnitude * (uint64_t)index;
        if (state->initialized != 0u && magnitude > state->previous_magnitude_q31[index]) {
            flux_sum += (uint64_t)(magnitude - state->previous_magnitude_q31[index]);
        }
        for (band = 0u; band < ODM_MUSIC_BAND_COUNT; band++) {
            if (index >= plan->band_bin_edges[band] &&
                index < plan->band_bin_edges[band + 1u]) {
                band_sums[band] += energy;
                break;
            }
        }
    }

    result.total_energy_q31 = music_average_u64(total_spectrum, ODM_MUSIC_FFT_BINS);
    for (index = 0u; index < ODM_MUSIC_BAND_COUNT; index++) {
        uint32_t count = plan->band_bin_edges[index + 1u] - plan->band_bin_edges[index];
        result.band_energy_q31[index] = music_average_u64(band_sums[index], count);
    }
    result.spectral_flux_q31 = state->initialized == 0u
                                   ? 0u
                                   : music_average_u64(flux_sum, ODM_MUSIC_FFT_BINS);

    if (magnitude_sum != 0u) {
        uint64_t whole = weighted_bins / magnitude_sum;
        uint64_t remainder = weighted_bins % magnitude_sum;
        uint64_t fractional = (remainder * UINT64_C(65536) + magnitude_sum / 2u) /
                              magnitude_sum;
        uint64_t centroid = whole * UINT64_C(65536) + fractional;
        if (centroid > UINT32_MAX) centroid = UINT32_MAX;
        result.spectral_centroid_bin_q16_16 = (uint32_t)centroid;
    }

    if (state->initialized == 0u) {
        result.envelope_short_q31 = result.total_energy_q31;
        result.envelope_medium_q31 = result.total_energy_q31;
        result.envelope_long_q31 = result.total_energy_q31;
        result.energy_delta_q31 = 0;
    } else {
        result.envelope_short_q31 = music_envelope_step(
            state->envelope_short_q31, result.total_energy_q31,
            plan->envelope_divisors[0]);
        result.envelope_medium_q31 = music_envelope_step(
            state->envelope_medium_q31, result.total_energy_q31,
            plan->envelope_divisors[1]);
        result.envelope_long_q31 = music_envelope_step(
            state->envelope_long_q31, result.total_energy_q31,
            plan->envelope_divisors[2]);
        result.energy_delta_q31 = (int64_t)result.total_energy_q31 -
                                  (int64_t)state->previous_total_energy_q31;
    }

    result.silence = (uint32_t)result.rms_mid_q31 <= plan->silence_rms_threshold_q31 ? 1u : 0u;
    {
        uint64_t lr_sum = (uint64_t)(uint32_t)result.rms_left_q31 +
                          (uint64_t)(uint32_t)result.rms_right_q31;
        int64_t lr_difference = (int64_t)result.rms_right_q31 -
                                (int64_t)result.rms_left_q31;
        uint64_t mid_side_sum = (uint64_t)(uint32_t)result.rms_mid_q31 +
                                (uint64_t)(uint32_t)result.rms_side_q31;
        result.stereo_balance_q31 = music_ratio_q31(lr_difference, lr_sum);
        result.stereo_width_q31 = (uint32_t)music_ratio_q31(
            (int64_t)result.rms_side_q31, mid_side_sum);
    }

    /* Transactional state/output publication happens only after all calculations. */
    memcpy(state->previous_magnitude_q31, scratch->magnitude_q31,
           sizeof(state->previous_magnitude_q31));
    state->previous_total_energy_q31 = result.total_energy_q31;
    state->envelope_short_q31 = result.envelope_short_q31;
    state->envelope_medium_q31 = result.envelope_medium_q31;
    state->envelope_long_q31 = result.envelope_long_q31;
    state->initialized = 1u;
    *out_tick = result;
    return ODM_STATUS_OK;
}

odm_status odm_music_analyze_map(
    const odm_music_analysis_plan *plan, const int32_t *window_q31,
    const odm_music_complex_q31 *twiddles_q31,
    const odm_pcm_stereo_q31 *canonical_pcm, uint64_t canonical_frames,
    odm_music_analysis_state *state, odm_music_analysis_scratch *scratch,
    odm_music_analysis_tick *ticks, uint64_t tick_capacity,
    uint64_t *out_required_ticks, const odm_job_ticket *cancel_ticket) {
    uint64_t required;
    uint64_t index;
    odm_status status;
    if (!plan || !window_q31 || !twiddles_q31 || !state || !scratch ||
        !out_required_ticks || (!canonical_pcm && canonical_frames != 0u)) {
        return ODM_STATUS_INVALID_ARGUMENT;
    }
    status = odm_music_tick_count(canonical_frames, &required);
    if (status != ODM_STATUS_OK) return status;
    *out_required_ticks = required;
    if (required == 0u) return ODM_STATUS_OK;
    if (!ticks || tick_capacity < required) return ODM_STATUS_BUFFER_TOO_SMALL;
    for (index = 0u; index < required; index++) {
        if (cancel_ticket && (index & UINT64_C(0x0f)) == 0u) {
            int cancelled = 0;
            status = odm_job_should_cancel(*cancel_ticket, &cancelled);
            if (status != ODM_STATUS_OK) return status;
            if (cancelled) return ODM_STATUS_CANCELLED;
        }
        status = odm_music_analyze_tick(plan, window_q31, twiddles_q31,
                                        canonical_pcm, canonical_frames, index,
                                        state, scratch, &ticks[index]);
        if (status != ODM_STATUS_OK) return status;
    }
    return ODM_STATUS_OK;
}
