/* Instrumento espectral directo. Contrato en odm_spectral_instrument.h. */

#include "odm_spectral_instrument.h"

#include "test_harness.h"

#include <string.h>

static odm_music_analysis_scratch si_scratch;

void odm_test_spectral_instrument(odm_test_context *context) {
    uint32_t i, prev_edge = 0u;

    /* Las bandas cubren intervalos crecientes y disjuntos, y cada ancla cae
     * dentro del suyo. Si un ancla cayera fuera, la banda estaria mostrando
     * una frecuencia que no es la que dice. */
    for (i = 0u; i <= ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++i) {
        uint32_t e = 0u;
        ODM_TEST_CHECK(context, odm_spectral_instrument_band_edge_q16(i, &e) == ODM_STATUS_OK);
        if (i != 0u) ODM_TEST_CHECK(context, e > prev_edge);
        prev_edge = e;
        if (i < ODM_SPECTRAL_INSTRUMENT_BAND_COUNT) {
            uint32_t anchor = 0u, bins = 0u, next = 0u;
            ODM_TEST_CHECK(context, odm_spectral_instrument_band_bin_q16(i, &anchor) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_spectral_instrument_band_bins_q16(i, &bins) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, odm_spectral_instrument_band_edge_q16(i + 1u, &next) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context, anchor >= e && anchor <= next);
            ODM_TEST_CHECK(context, bins == next - e);
        }
    }
    {
        uint32_t tmp = 0u;
        ODM_TEST_CHECK(context,
            odm_spectral_instrument_band_bin_q16(ODM_SPECTRAL_INSTRUMENT_BAND_COUNT, &tmp)
                == ODM_STATUS_INVALID_ARGUMENT);
        ODM_TEST_CHECK(context,
            odm_spectral_instrument_band_edge_q16(ODM_SPECTRAL_INSTRUMENT_BAND_COUNT + 1u, &tmp)
                == ODM_STATUS_INVALID_ARGUMENT);
    }

    /* log2: exacto en las potencias de dos, monotono, y sin desbordar. */
    ODM_TEST_CHECK(context, odm_spectral_instrument_log2_q16(0u) == 0u);
    ODM_TEST_CHECK(context, odm_spectral_instrument_log2_q16(1u) == 0u);
    for (i = 1u; i < 31u; ++i) {
        uint32_t l = odm_spectral_instrument_log2_q16(UINT32_C(1) << i);
        ODM_TEST_CHECK(context, l == (i << 16));
    }
    {
        uint32_t anterior = 0u;
        for (i = 1u; i < 100000u; i += 7u) {
            uint32_t l = odm_spectral_instrument_log2_q16(i);
            ODM_TEST_CHECK(context, l >= anterior);
            anterior = l;
        }
    }

    /* Un espectro plano da la misma media en todas las bandas: la integracion
     * no puede inventar estructura donde no la hay. */
    {
        odm_music_analysis_tick base;
        odm_spectral_instrument_tick tick;
        uint32_t b;
        for (b = 0u; b < ODM_MUSIC_FFT_BINS; ++b) si_scratch.magnitude_q31[b] = 1000000u;
        memset(&base, 0, sizeof(base));
        ODM_TEST_CHECK(context,
            odm_spectral_instrument_extract_tick(&base, &si_scratch, &tick) == ODM_STATUS_OK);
        for (b = 0u; b < ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++b) {
            ODM_TEST_CHECK(context, tick.band_magnitude_q31[b] == 1000000u);
            ODM_TEST_CHECK(context, tick.band_presence_q31[b] == tick.band_presence_q31[0]);
        }
        /* Y el silencio es silencio, sin suelo espurio. */
        for (b = 0u; b < ODM_MUSIC_FFT_BINS; ++b) si_scratch.magnitude_q31[b] = 0u;
        ODM_TEST_CHECK(context,
            odm_spectral_instrument_extract_tick(&base, &si_scratch, &tick) == ODM_STATUS_OK);
        for (b = 0u; b < ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++b) {
            ODM_TEST_CHECK(context, tick.band_magnitude_q31[b] == 0u);
            ODM_TEST_CHECK(context, tick.band_presence_q31[b] == 0u);
        }
        /* Monotonia de la transferencia: mas magnitud nunca da menos presencia. */
        {
            uint32_t previa = 0u, paso;
            for (paso = 1u; paso < 40u; ++paso) {
                uint32_t nivel = paso * 40000000u;
                for (b = 0u; b < ODM_MUSIC_FFT_BINS; ++b) si_scratch.magnitude_q31[b] = nivel;
                ODM_TEST_CHECK(context,
                    odm_spectral_instrument_extract_tick(&base, &si_scratch, &tick) == ODM_STATUS_OK);
                ODM_TEST_CHECK(context, tick.band_presence_q31[0] >= previa);
                previa = tick.band_presence_q31[0];
            }
        }
    }

    /* Proyeccion centrada: en un centro exacto no interpola. */
    {
        odm_spectral_instrument_tick ts[2];
        odm_spectral_instrument_projection pr;
        uint32_t b;
        memset(ts, 0, sizeof(ts));
        for (i = 0u; i < 2u; ++i) {
            ts[i].schema_version = ODM_SPECTRAL_INSTRUMENT_SCHEMA_VERSION;
            ts[i].flags = ODM_SPECTRAL_INSTRUMENT_FLAG_SHARED_LOG_SCALE |
                          ODM_SPECTRAL_INSTRUMENT_FLAG_DIRECT_FFT |
                          ODM_SPECTRAL_INSTRUMENT_FLAG_BAND_INTEGRATION |
                          ODM_SPECTRAL_INSTRUMENT_FLAG_EXACT_LOG2;
            ts[i].tick_index = i;
            ts[i].center_sample = (uint64_t)i * (uint64_t)ODM_MUSIC_TICK_SAMPLES;
            for (b = 0u; b < ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++b)
                ts[i].band_presence_q31[b] = i == 0u ? 0u : (uint32_t)INT32_MAX;
        }
        ODM_TEST_CHECK(context,
            odm_spectral_instrument_project_centered(ts, 2u, 0u, &pr) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, pr.upper_weight_q31 == 0u);
        ODM_TEST_CHECK(context, pr.band_presence_q31[0] == 0u);
        ODM_TEST_CHECK(context,
            odm_spectral_instrument_project_centered(ts, 2u, 480u, &pr) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, pr.band_presence_q31[0] == (uint32_t)INT32_MAX);
        /* A mitad de camino, la mitad exacta. */
        ODM_TEST_CHECK(context,
            odm_spectral_instrument_project_centered(ts, 2u, 240u, &pr) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, pr.band_presence_q31[0] > 1000000000u);
        ODM_TEST_CHECK(context, pr.band_presence_q31[0] < 1147483647u);

        /* Aplicado a un cuadro, la provenance describe el extremo. */
        {
            odm_composition_frame_state frame;
            memset(&frame, 0, sizeof(frame));
            frame.schema_version = ODM_COMPOSITION_SCHEMA_VERSION;
            frame.center_sample = 240u;
            frame.flags = ODM_COMPOSITION_FLAG_RADIAL_HIRES | ODM_COMPOSITION_FLAG_STRICT_CAUSAL |
                          ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE | ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE;
            for (b = 0u; b < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++b) {
                frame.radial_body_q31[b] = 42u;
                frame.radial_release_q31[b] = 42u;
                frame.radial_attack_q31[b] = 42u;
            }
            ODM_TEST_CHECK(context,
                odm_composition_apply_spectral_instrument_projection(&pr, ODM_SPECTRAL_SYMMETRY_NONE, &frame) == ODM_STATUS_OK);
            ODM_TEST_CHECK(context,
                (frame.flags & ODM_COMPOSITION_FLAG_DIRECT_SPECTRAL_INSTRUMENT) != 0u);
            for (b = 0u; b < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++b) {
                ODM_TEST_CHECK(context, frame.radial_q31[b] == pr.band_presence_q31[b]);
                ODM_TEST_CHECK(context, frame.radial_body_q31[b] == frame.radial_q31[b]);
                ODM_TEST_CHECK(context, frame.radial_release_q31[b] == 0u);
                ODM_TEST_CHECK(context, frame.radial_attack_q31[b] == 0u);
            }
        }
    }

    /* Identidad de politica. */
    {
        uint8_t bytes[ODM_SPECTRAL_INSTRUMENT_POLICY_BYTES];
        uint64_t required = 0u;
        odm_sha256_digest a, b;
        ODM_TEST_CHECK(context,
            odm_spectral_instrument_policy_bytes(NULL, 0u, &required) == ODM_STATUS_BUFFER_TOO_SMALL);
        ODM_TEST_CHECK(context, required == ODM_SPECTRAL_INSTRUMENT_POLICY_BYTES);
        ODM_TEST_CHECK(context,
            odm_spectral_instrument_policy_bytes(bytes, sizeof(bytes), &required) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, memcmp(bytes, "ODMSPEC3", 8u) == 0);
        ODM_TEST_CHECK(context, odm_spectral_instrument_policy_current_sha256(&a) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_sha256(bytes, sizeof(bytes), &b) == ODM_STATUS_OK);
        ODM_TEST_CHECK(context, odm_sha256_equal(&a, &b));
    }
}
