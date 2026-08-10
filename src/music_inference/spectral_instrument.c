/* Instrumento espectral directo. Contrato y justificacion en
 * odm_spectral_instrument.h.
 *
 * Nada en este archivo usa coma flotante. Las dos tablas congeladas se
 * generaron una vez desde la ley declarada -- hz = 34*(15000/34)^(i/95) -- con
 * aritmetica decimal de 60 digitos, y el oraculo independiente las reconstruye
 * desde esa misma ley en lugar de copiarlas.
 */

#include "odm_spectral_instrument.h"

#include "odm_wire.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define SI_Q31 ((uint32_t)INT32_MAX)
/* Ventana perceptual: 2^15 y 2^27 sobre un fondo de escala de 2^31, es decir
 * unos -96 dBFS a -24 dBFS. Todo lo que quede por debajo es silencio visual y
 * todo lo que quede por encima satura la aguja. */
#define SI_LOG_FLOOR_Q16 (UINT32_C(15) << 16)
#define SI_LOG_CEIL_Q16  (UINT32_C(27) << 16)
#define SI_LOG_RANGE_Q16 (SI_LOG_CEIL_Q16 - SI_LOG_FLOOR_Q16)

static const uint32_t si_band_bin_q16[ODM_SPECTRAL_INSTRUMENT_BAND_COUNT] = {
    UINT32_C(95071), UINT32_C(101364), UINT32_C(108075), UINT32_C(115229),
    UINT32_C(122857), UINT32_C(130990), UINT32_C(139661), UINT32_C(148907),
    UINT32_C(158764), UINT32_C(169274), UINT32_C(180479), UINT32_C(192427),
    UINT32_C(205165), UINT32_C(218747), UINT32_C(233228), UINT32_C(248667),
    UINT32_C(265128), UINT32_C(282679), UINT32_C(301392), UINT32_C(321344),
    UINT32_C(342616), UINT32_C(365297), UINT32_C(389479), UINT32_C(415262),
    UINT32_C(442752), UINT32_C(472061), UINT32_C(503311), UINT32_C(536629),
    UINT32_C(572153), UINT32_C(610029), UINT32_C(650412), UINT32_C(693468),
    UINT32_C(739374), UINT32_C(788320), UINT32_C(840505), UINT32_C(896145),
    UINT32_C(955469), UINT32_C(1018719), UINT32_C(1086157), UINT32_C(1158059),
    UINT32_C(1234720), UINT32_C(1316457), UINT32_C(1403604), UINT32_C(1496520),
    UINT32_C(1595588), UINT32_C(1701213), UINT32_C(1813830), UINT32_C(1933903),
    UINT32_C(2061924), UINT32_C(2198421), UINT32_C(2343952), UINT32_C(2499118),
    UINT32_C(2664556), UINT32_C(2840945), UINT32_C(3029011), UINT32_C(3229527),
    UINT32_C(3443316), UINT32_C(3671259), UINT32_C(3914290), UINT32_C(4173410),
    UINT32_C(4449683), UINT32_C(4744245), UINT32_C(5058306), UINT32_C(5393158),
    UINT32_C(5750176), UINT32_C(6130828), UINT32_C(6536679), UINT32_C(6969397),
    UINT32_C(7430760), UINT32_C(7922664), UINT32_C(8447132), UINT32_C(9006319),
    UINT32_C(9602523), UINT32_C(10238194), UINT32_C(10915946), UINT32_C(11638564),
    UINT32_C(12409019), UINT32_C(13230476), UINT32_C(14106312), UINT32_C(15040127),
    UINT32_C(16035760), UINT32_C(17097301), UINT32_C(18229115), UINT32_C(19435853),
    UINT32_C(20722475), UINT32_C(22094270), UINT32_C(23556875), UINT32_C(25116303),
    UINT32_C(26778962), UINT32_C(28551686), UINT32_C(30441762), UINT32_C(32456957),
    UINT32_C(34605556), UINT32_C(36896388), UINT32_C(39338870), UINT32_C(41943040)
};

static const uint32_t si_band_edge_q16[ODM_SPECTRAL_INSTRUMENT_BAND_COUNT + 1u] = {
    UINT32_C(92072), UINT32_C(98167), UINT32_C(104666), UINT32_C(111594),
    UINT32_C(118982), UINT32_C(126858), UINT32_C(135256), UINT32_C(144210),
    UINT32_C(153756), UINT32_C(163935), UINT32_C(174787), UINT32_C(186357),
    UINT32_C(198694), UINT32_C(211847), UINT32_C(225871), UINT32_C(240824),
    UINT32_C(256766), UINT32_C(273763), UINT32_C(291886), UINT32_C(311208),
    UINT32_C(331810), UINT32_C(353775), UINT32_C(377194), UINT32_C(402164),
    UINT32_C(428787), UINT32_C(457172), UINT32_C(487436), UINT32_C(519703),
    UINT32_C(554106), UINT32_C(590787), UINT32_C(629897), UINT32_C(671595),
    UINT32_C(716053), UINT32_C(763455), UINT32_C(813994), UINT32_C(867879),
    UINT32_C(925332), UINT32_C(986587), UINT32_C(1051898), UINT32_C(1121532),
    UINT32_C(1195775), UINT32_C(1274934), UINT32_C(1359332), UINT32_C(1449318),
    UINT32_C(1545260), UINT32_C(1647554), UINT32_C(1756619), UINT32_C(1872905),
    UINT32_C(1996888), UINT32_C(2129079), UINT32_C(2270021), UINT32_C(2420292),
    UINT32_C(2580512), UINT32_C(2751337), UINT32_C(2933472), UINT32_C(3127663),
    UINT32_C(3334709), UINT32_C(3555461), UINT32_C(3790827), UINT32_C(4041774),
    UINT32_C(4309333), UINT32_C(4594604), UINT32_C(4898759), UINT32_C(5223049),
    UINT32_C(5568807), UINT32_C(5937453), UINT32_C(6330502), UINT32_C(6749571),
    UINT32_C(7196382), UINT32_C(7672771), UINT32_C(8180696), UINT32_C(8722245),
    UINT32_C(9299644), UINT32_C(9915266), UINT32_C(10571640), UINT32_C(11271466),
    UINT32_C(12017619), UINT32_C(12813166), UINT32_C(13661377), UINT32_C(14565738),
    UINT32_C(15529967), UINT32_C(16558026), UINT32_C(17654140), UINT32_C(18822816),
    UINT32_C(20068856), UINT32_C(21397382), UINT32_C(22813855), UINT32_C(24324095),
    UINT32_C(25934311), UINT32_C(27651121), UINT32_C(29481581), UINT32_C(31433214),
    UINT32_C(33514043), UINT32_C(35732619), UINT32_C(38098061), UINT32_C(40620091),
    UINT32_C(43309076)
};

/* Division con redondeo al mas cercano, mitades alejandose de cero. Es la misma
 * convencion que usa el resto del motor, y usarla aqui evita que dos modulos
 * redondeen distinto sobre el mismo dato. */
static int64_t si_div_round(int64_t num, int64_t den) {
    int64_t q = num / den, r = num % den;
    int64_t ar = r < 0 ? -r : r, ad = den < 0 ? -den : den;
    if (ar * 2 >= ad) q += ((num < 0) != (den < 0)) ? -1 : 1;
    return q;
}

uint32_t odm_spectral_instrument_log2_q16(uint32_t value) {
    /* log2 exacto por cuadrados sucesivos. El exponente sale del recuento de
     * bits y es exacto; cada iteracion eleva la mantisa al cuadrado y emite un
     * bit del resto. Dieciseis iteraciones dan los dieciseis bits fraccionarios
     * de Q16.16, con error por debajo de 1 LSB.
     *
     * La mantisa se mantiene en Q30 y no en Q32 a proposito: en Q30 el cuadrado
     * cabe en 64 bits sin recurrir a enteros de 128, que no existen en C11
     * portable. */
    uint32_t p = 0u, i, frac = 0u;
    uint64_t m;
    if (value == 0u) return 0u;
    { uint32_t t = value; while (t > 1u) { t >>= 1; ++p; } }
    m = ((uint64_t)value << 30) >> p;          /* mantisa en [1,2), Q30 */
    for (i = 0u; i < 16u; ++i) {
        m = (m * m) >> 30;                     /* en [1,4), Q30 */
        frac <<= 1;
        if (m >= (UINT64_C(1) << 31)) { m >>= 1; frac |= 1u; }
    }
    return (p << 16) | frac;
}

static uint32_t si_presence(uint32_t magnitude) {
    uint32_t l;
    uint64_t n;
    if (magnitude == 0u) return 0u;
    l = odm_spectral_instrument_log2_q16(magnitude);
    if (l <= SI_LOG_FLOOR_Q16) return 0u;
    if (l >= SI_LOG_CEIL_Q16) return SI_Q31;
    n = (uint64_t)(l - SI_LOG_FLOOR_Q16) * (uint64_t)SI_Q31 +
        (uint64_t)SI_LOG_RANGE_Q16 / 2u;
    return (uint32_t)(n / (uint64_t)SI_LOG_RANGE_Q16);
}

/* Media exacta del espectro sobre [lo,hi) en coordenadas de bin Q16.16.
 *
 * El espectro se trata como la funcion lineal a trozos que pasa por los centros
 * de bin, y se integra esa funcion. Sobre un tramo dentro de un bin la integral
 * de una recta es su valor en el punto medio por la longitud del tramo, asi que
 * el bucle acumula exactamente eso. El punto medio se calcula sin perder medio
 * LSB usando (u+v) contra un denominador doble.
 *
 * Consecuencia buscada: si la banda es mas ancha que un bin, integra toda su
 * energia; si es mas estrecha -- las graves lo son -- devuelve el valor
 * interpolado en su centro. Una sola formula, sin salto en el cruce. */
static uint32_t si_band_mean(const uint32_t *mag, uint32_t lo_q16, uint32_t hi_q16) {
    const uint32_t last = ODM_MUSIC_FFT_BINS - 1u;
    const uint32_t top_q16 = last << 16;
    uint64_t acc = 0u;
    uint32_t k, k_end;
    if (lo_q16 > top_q16) lo_q16 = top_q16;
    if (hi_q16 > top_q16) hi_q16 = top_q16;
    if (hi_q16 <= lo_q16) {
        /* Banda degenerada tras el recorte: valor interpolado en el punto. */
        uint32_t base = lo_q16 >> 16, t = lo_q16 & 0xffffu;
        int64_t slope;
        if (base >= last) return mag[last];
        slope = (int64_t)mag[base + 1u] - (int64_t)mag[base];
        return (uint32_t)((int64_t)mag[base] + si_div_round(slope * (int64_t)t, 65536));
    }
    k = lo_q16 >> 16;
    k_end = (hi_q16 - 1u) >> 16;
    for (; k <= k_end; ++k) {
        uint32_t seg_lo = (k << 16) > lo_q16 ? (k << 16) : lo_q16;
        uint32_t seg_hi = ((k + 1u) << 16) < hi_q16 ? ((k + 1u) << 16) : hi_q16;
        uint32_t u, v, len;
        int64_t slope, val;
        if (seg_hi <= seg_lo) continue;
        u = seg_lo - (k << 16);
        v = seg_hi - (k << 16);
        len = seg_hi - seg_lo;
        slope = (k < last) ? ((int64_t)mag[k + 1u] - (int64_t)mag[k]) : 0;
        /* valor en el punto medio: mag[k] + pendiente*(u+v)/(2*65536) */
        val = (int64_t)mag[k] + si_div_round(slope * (int64_t)(u + v), 131072);
        if (val < 0) val = 0;
        acc += (uint64_t)val * (uint64_t)len;
    }
    {
        uint64_t den = (uint64_t)(hi_q16 - lo_q16);
        uint64_t r = (acc + den / 2u) / den;
        return r > (uint64_t)SI_Q31 ? SI_Q31 : (uint32_t)r;
    }
}

odm_status odm_spectral_instrument_band_bin_q16(uint32_t band, uint32_t *out_bin_q16) {
    if (!out_bin_q16 || band >= ODM_SPECTRAL_INSTRUMENT_BAND_COUNT)
        return ODM_STATUS_INVALID_ARGUMENT;
    *out_bin_q16 = si_band_bin_q16[band];
    return ODM_STATUS_OK;
}

odm_status odm_spectral_instrument_band_edge_q16(uint32_t edge, uint32_t *out_bin_q16) {
    if (!out_bin_q16 || edge > ODM_SPECTRAL_INSTRUMENT_BAND_COUNT)
        return ODM_STATUS_INVALID_ARGUMENT;
    *out_bin_q16 = si_band_edge_q16[edge];
    return ODM_STATUS_OK;
}

odm_status odm_spectral_instrument_band_bins_q16(uint32_t band, uint32_t *out_bins_q16) {
    if (!out_bins_q16 || band >= ODM_SPECTRAL_INSTRUMENT_BAND_COUNT)
        return ODM_STATUS_INVALID_ARGUMENT;
    *out_bins_q16 = si_band_edge_q16[band + 1u] - si_band_edge_q16[band];
    return ODM_STATUS_OK;
}

odm_status odm_spectral_instrument_extract_tick(
    const odm_music_analysis_tick *base_tick,
    const odm_music_analysis_scratch *scratch,
    odm_spectral_instrument_tick *out_tick) {
    uint32_t i;
    if (!base_tick || !scratch || !out_tick) return ODM_STATUS_INVALID_ARGUMENT;
    memset(out_tick, 0, sizeof(*out_tick));
    out_tick->schema_version = ODM_SPECTRAL_INSTRUMENT_SCHEMA_VERSION;
    out_tick->flags = ODM_SPECTRAL_INSTRUMENT_FLAG_SHARED_LOG_SCALE |
                      ODM_SPECTRAL_INSTRUMENT_FLAG_DIRECT_FFT |
                      ODM_SPECTRAL_INSTRUMENT_FLAG_BAND_INTEGRATION |
                      ODM_SPECTRAL_INSTRUMENT_FLAG_EXACT_LOG2;
    out_tick->tick_index = base_tick->tick_index;
    out_tick->center_sample = base_tick->center_sample;
    for (i = 0u; i < ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++i) {
        uint32_t m = si_band_mean(scratch->magnitude_q31,
                                  si_band_edge_q16[i], si_band_edge_q16[i + 1u]);
        out_tick->band_magnitude_q31[i] = m;
        out_tick->band_presence_q31[i] = si_presence(m);
    }
    return ODM_STATUS_OK;
}

static int si_tick_valid(const odm_spectral_instrument_tick *t) {
    const uint32_t required = ODM_SPECTRAL_INSTRUMENT_FLAG_SHARED_LOG_SCALE |
                              ODM_SPECTRAL_INSTRUMENT_FLAG_DIRECT_FFT |
                              ODM_SPECTRAL_INSTRUMENT_FLAG_BAND_INTEGRATION |
                              ODM_SPECTRAL_INSTRUMENT_FLAG_EXACT_LOG2;
    uint32_t i;
    if (!t || t->schema_version != ODM_SPECTRAL_INSTRUMENT_SCHEMA_VERSION) return 0;
    if ((t->flags & required) != required) return 0;
    if (t->center_sample != t->tick_index * (uint64_t)ODM_MUSIC_TICK_SAMPLES) return 0;
    for (i = 0u; i < ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++i)
        if (t->band_presence_q31[i] > SI_Q31 || t->band_magnitude_q31[i] > SI_Q31) return 0;
    for (i = 0u; i < 8u; ++i) if (t->reserved[i] != 0u) return 0;
    return 1;
}

static uint32_t si_lerp(uint32_t a, uint32_t b, uint32_t t_q31) {
    uint64_t inv = (uint64_t)SI_Q31 - (uint64_t)t_q31;
    uint64_t n = (uint64_t)a * inv + (uint64_t)b * (uint64_t)t_q31 + (uint64_t)SI_Q31 / 2u;
    n /= (uint64_t)SI_Q31;
    return n > (uint64_t)SI_Q31 ? SI_Q31 : (uint32_t)n;
}

odm_status odm_spectral_instrument_core_drive(
    const odm_spectral_instrument_projection *projection,
    uint32_t band_lo, uint32_t band_hi, uint32_t *out_drive_q31) {
    uint64_t acc = 0u;
    uint32_t b, n;
    if (!projection || !out_drive_q31) return ODM_STATUS_INVALID_ARGUMENT;
    if (band_hi < band_lo || band_hi >= ODM_SPECTRAL_INSTRUMENT_BAND_COUNT)
        return ODM_STATUS_INVALID_ARGUMENT;
    n = band_hi - band_lo + 1u;
    for (b = band_lo; b <= band_hi; ++b) acc += projection->band_presence_q31[b];
    *out_drive_q31 = (uint32_t)((acc + n / 2u) / n);
    return ODM_STATUS_OK;
}

odm_status odm_spectral_instrument_symmetry_band(uint32_t symmetry, uint32_t sector,
                                                 uint32_t *out_band) {
    const uint32_t n = ODM_SPECTRAL_INSTRUMENT_BAND_COUNT;
    if (!out_band || sector >= n) return ODM_STATUS_INVALID_ARGUMENT;
    switch (symmetry) {
        case ODM_SPECTRAL_SYMMETRY_NONE:
            *out_band = sector;
            return ODM_STATUS_OK;
        case ODM_SPECTRAL_SYMMETRY_MIRROR: {
            /* Cada mitad recorre el espectro entero, asi que la corona es
             * simetrica respecto al eje vertical. Se usan bandas pares para
             * cubrir todo el rango con la mitad de sectores. */
            uint32_t m = sector < n / 2u ? sector : (n - 1u - sector);
            *out_band = (m * 2u) < n ? m * 2u : n - 1u;
            return ODM_STATUS_OK;
        }
        case ODM_SPECTRAL_SYMMETRY_QUADRANT: {
            uint32_t q = sector % (n / 2u);
            uint32_t m = q < n / 4u ? q : (n / 2u - 1u - q);
            *out_band = (m * 4u) < n ? m * 4u : n - 1u;
            return ODM_STATUS_OK;
        }
        default:
            return ODM_STATUS_INVALID_ARGUMENT;
    }
}

odm_status odm_spectral_instrument_smooth(odm_spectral_instrument_tick *ticks,
                                          uint64_t tick_count,
                                          uint32_t rise_q31,
                                          uint32_t fall_q31,
                                          uint32_t band_blur) {
    const uint32_t n = ODM_SPECTRAL_INSTRUMENT_BAND_COUNT;
    uint64_t t;
    uint32_t b, pass;
    uint32_t estado[ODM_SPECTRAL_INSTRUMENT_BAND_COUNT];
    uint32_t tmp[ODM_SPECTRAL_INSTRUMENT_BAND_COUNT];
    if (!ticks || tick_count == 0u) return ODM_STATUS_INVALID_ARGUMENT;
    if (rise_q31 > SI_Q31 || fall_q31 > SI_Q31 || band_blur > 4u)
        return ODM_STATUS_INVALID_ARGUMENT;

    /* Lateral primero: suavizar en frecuencia y despues en tiempo evita que el
     * filtro temporal persiga un peine que el lateral va a borrar de todos
     * modos. */
    for (pass = 0u; pass < band_blur; ++pass) {
        for (t = 0u; t < tick_count; ++t) {
            uint32_t *v = ticks[t].band_presence_q31;
            for (b = 0u; b < n; ++b) {
                uint64_t izq = v[b == 0u ? 0u : b - 1u];
                uint64_t der = v[b + 1u < n ? b + 1u : n - 1u];
                tmp[b] = (uint32_t)((izq + 2u * (uint64_t)v[b] + der + 2u) / 4u);
            }
            for (b = 0u; b < n; ++b) v[b] = tmp[b];
        }
    }

    for (b = 0u; b < n; ++b) estado[b] = ticks[0].band_presence_q31[b];
    for (t = 0u; t < tick_count; ++t) {
        uint32_t *v = ticks[t].band_presence_q31;
        for (b = 0u; b < n; ++b) {
            uint32_t x = v[b], y = estado[b];
            uint32_t w = x > y ? rise_q31 : fall_q31;
            uint64_t d = x > y ? (uint64_t)(x - y) : (uint64_t)(y - x);
            uint32_t paso = (uint32_t)((d * (uint64_t)w) / (uint64_t)SI_Q31);
            y = x > y ? y + paso : y - paso;
            estado[b] = y;
            v[b] = y;
        }
    }
    return ODM_STATUS_OK;
}

odm_status odm_spectral_instrument_project_centered(
    const odm_spectral_instrument_tick *ticks,
    uint64_t tick_count,
    uint64_t presentation_sample,
    odm_spectral_instrument_projection *out_projection) {
    uint64_t lower, upper, lo_c, hi_c;
    uint32_t w = 0u, i;
    if (!ticks || !out_projection || tick_count == 0u) return ODM_STATUS_INVALID_ARGUMENT;

    lower = presentation_sample / (uint64_t)ODM_MUSIC_TICK_SAMPLES;
    if (lower >= tick_count) lower = tick_count - 1u;
    upper = lower + 1u < tick_count ? lower + 1u : lower;
    if (!si_tick_valid(&ticks[lower]) || !si_tick_valid(&ticks[upper]))
        return ODM_STATUS_INVALID_DATA;
    lo_c = ticks[lower].center_sample;
    hi_c = ticks[upper].center_sample;
    if (hi_c > lo_c && presentation_sample > lo_c) {
        uint64_t span = hi_c - lo_c;
        uint64_t off = presentation_sample - lo_c;
        if (off > span) off = span;
        w = (uint32_t)((off * (uint64_t)SI_Q31 + span / 2u) / span);
    }

    memset(out_projection, 0, sizeof(*out_projection));
    out_projection->schema_version = ODM_SPECTRAL_INSTRUMENT_SCHEMA_VERSION;
    out_projection->flags = ticks[lower].flags |
                            ODM_SPECTRAL_INSTRUMENT_FLAG_CENTERED_INTERPOLATION;
    out_projection->presentation_sample = presentation_sample;
    out_projection->lower_tick_index = ticks[lower].tick_index;
    out_projection->upper_tick_index = ticks[upper].tick_index;
    out_projection->lower_center_sample = lo_c;
    out_projection->upper_center_sample = hi_c;
    out_projection->upper_weight_q31 = w;
    for (i = 0u; i < ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++i) {
        out_projection->band_presence_q31[i] =
            si_lerp(ticks[lower].band_presence_q31[i],
                    ticks[upper].band_presence_q31[i], w);
    }
    return ODM_STATUS_OK;
}

odm_status odm_composition_apply_spectral_instrument_projection(
    const odm_spectral_instrument_projection *projection,
    uint32_t symmetry,
    odm_composition_frame_state *in_out_frame) {
    const uint32_t required = ODM_COMPOSITION_FLAG_RADIAL_HIRES |
                              ODM_COMPOSITION_FLAG_STRICT_CAUSAL |
                              ODM_COMPOSITION_FLAG_RADIAL_PROVENANCE |
                              ODM_COMPOSITION_FLAG_RADIAL_TIMESCALE;
    const uint32_t need = ODM_SPECTRAL_INSTRUMENT_FLAG_SHARED_LOG_SCALE |
                          ODM_SPECTRAL_INSTRUMENT_FLAG_DIRECT_FFT |
                          ODM_SPECTRAL_INSTRUMENT_FLAG_BAND_INTEGRATION |
                          ODM_SPECTRAL_INSTRUMENT_FLAG_EXACT_LOG2 |
                          ODM_SPECTRAL_INSTRUMENT_FLAG_CENTERED_INTERPOLATION;
    uint32_t i;
    if (!projection || !in_out_frame) return ODM_STATUS_INVALID_ARGUMENT;
    if (projection->schema_version != ODM_SPECTRAL_INSTRUMENT_SCHEMA_VERSION ||
        (projection->flags & need) != need ||
        projection->upper_weight_q31 > SI_Q31)
        return ODM_STATUS_INVALID_DATA;
    if (in_out_frame->schema_version != ODM_COMPOSITION_SCHEMA_VERSION ||
        (in_out_frame->flags & required) != required)
        return ODM_STATUS_INVALID_DATA;
    /* La proyeccion tiene que describir la misma vecindad de presentacion que
     * el cuadro: mas de un intervalo de analisis de distancia solo puede ser
     * una confusion de indices en quien llama. */
    if (projection->presentation_sample + (uint64_t)ODM_MUSIC_TICK_SAMPLES <
            in_out_frame->center_sample ||
        in_out_frame->center_sample + (uint64_t)ODM_MUSIC_TICK_SAMPLES <
            projection->presentation_sample)
        return ODM_STATUS_INVALID_DATA;

    if (symmetry >= ODM_SPECTRAL_SYMMETRY_COUNT) return ODM_STATUS_INVALID_ARGUMENT;
    for (i = 0u; i < ODM_COMPOSITION_RADIAL_SEGMENTS_MAX; ++i) {
        uint32_t banda = i, v;
        odm_status sb = odm_spectral_instrument_symmetry_band(symmetry, i, &banda);
        if (sb != ODM_STATUS_OK) return sb;
        v = projection->band_presence_q31[banda];
        if (v > SI_Q31) return ODM_STATUS_INVALID_DATA;
        in_out_frame->radial_q31[i] = v;
        /* La provenance describe el valor que acompana, no el que hubo antes.
         * Bajo este gobierno el extremo ES la medida directa: cuerpo = medida,
         * sin cola ni ataque que explicar. Dejar aqui la descomposicion de
         * Music-Reaction seria publicar una explicacion de otro numero. */
        in_out_frame->radial_body_q31[i] = v;
        in_out_frame->radial_release_q31[i] = 0u;
        in_out_frame->radial_attack_q31[i] = 0u;
    }
    {
        /* El nucleo respira con los graves medidos, sobre el mismo intervalo
         * [0.48, 0.58] que ya usa la composicion, para que todo lo que
         * normaliza aguas abajo -- la energia del Director, entre otros --
         * siga leyendo la misma escala. */
        const uint32_t base_q31 = UINT32_C(1030792151); /* 0.48 */
        const uint32_t alto_q31 = UINT32_C(1245540515); /* 0.58 */
        uint32_t drive = 0u;
        odm_status sd = odm_spectral_instrument_core_drive(projection,
                                                           ODM_SPECTRAL_CORE_BAND_LO,
                                                           ODM_SPECTRAL_CORE_BAND_HI,
                                                           &drive);
        if (sd != ODM_STATUS_OK) return sd;
        in_out_frame->core_scale_q31 = base_q31 +
            (uint32_t)(((uint64_t)(alto_q31 - base_q31) * (uint64_t)drive) / (uint64_t)SI_Q31);
    }
    in_out_frame->flags |= ODM_COMPOSITION_FLAG_DIRECT_SPECTRAL_INSTRUMENT;
    return ODM_STATUS_OK;
}

odm_status odm_spectral_instrument_policy_bytes(uint8_t *buffer, uint64_t capacity,
                                                uint64_t *out_required) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    odm_status st;
    uint32_t i;
    const uint64_t bytes = ODM_SPECTRAL_INSTRUMENT_POLICY_BYTES;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = bytes;
    if (!buffer || capacity < bytes) return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, (size_t)bytes);
    st = odm_wire_writer_init(&w, buffer, bytes);
    if (st != ODM_STATUS_OK) return st;
#define SP(x) do { st = (x); if (st != ODM_STATUS_OK) return st; } while (0)
    SP(odm_wire_write_bytes(&w, "ODMSPEC3", 8u));
    SP(odm_wire_write_u32(&w, ODM_SPECTRAL_INSTRUMENT_POLICY_VERSION));
    SP(odm_wire_write_u32(&w, ODM_SPECTRAL_INSTRUMENT_SCHEMA_VERSION));
    SP(odm_wire_write_u32(&w, ODM_SPECTRAL_INSTRUMENT_BAND_COUNT));
    SP(odm_wire_write_u32(&w, ODM_SPECTRAL_INSTRUMENT_MIN_HZ));
    SP(odm_wire_write_u32(&w, ODM_SPECTRAL_INSTRUMENT_MAX_HZ));
    SP(odm_wire_write_u32(&w, ODM_MUSIC_SAMPLE_RATE));
    SP(odm_wire_write_u32(&w, ODM_MUSIC_WINDOW_SAMPLES));
    SP(odm_wire_write_u32(&w, ODM_MUSIC_FFT_BINS));
    SP(odm_wire_write_u32(&w, SI_LOG_FLOOR_Q16));
    SP(odm_wire_write_u32(&w, SI_LOG_CEIL_Q16));
    SP(odm_wire_write_u32(&w, 1u)); /* media integrada sobre el intervalo de banda */
    SP(odm_wire_write_u32(&w, 1u)); /* espectro lineal a trozos entre centros de bin */
    SP(odm_wire_write_u32(&w, 1u)); /* log2 exacto bit a bit, no mantisa lineal */
    SP(odm_wire_write_u32(&w, 1u)); /* la interpolacion centrada no es causal */
    SP(odm_wire_write_u32(&w, 1u)); /* la provenance describe la medida directa */
    SP(odm_wire_write_u32(&w, ODM_SPECTRAL_SYMMETRY_COUNT)); /* simetrias del arco */
    SP(odm_wire_write_u32(&w, 4u)); /* nucleo lateral [1,2,1]/4, hasta 4 pasadas */
    SP(odm_wire_write_u32(&w, 1u)); /* el polo temporal actua sobre ticks, no sobre cuadros */
    SP(odm_wire_write_u32(&w, ODM_SPECTRAL_CORE_BAND_LO));
    SP(odm_wire_write_u32(&w, ODM_SPECTRAL_CORE_BAND_HI));
    for (i = 0u; i <= ODM_SPECTRAL_INSTRUMENT_BAND_COUNT; ++i)
        SP(odm_wire_write_u32(&w, si_band_edge_q16[i]));
#undef SP
    return odm_wire_writer_finish(&w, out_required);
}

odm_status odm_spectral_instrument_policy_current_sha256(odm_sha256_digest *out_hash) {
    uint8_t bytes[ODM_SPECTRAL_INSTRUMENT_POLICY_BYTES];
    uint64_t required = 0u;
    odm_status st;
    if (!out_hash) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_spectral_instrument_policy_bytes(bytes, sizeof(bytes), &required);
    if (st != ODM_STATUS_OK) return st;
    return odm_sha256(bytes, sizeof(bytes), out_hash);
}
