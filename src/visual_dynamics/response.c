/* Visual Dynamics v1 — sample-domain causal response.
 * Normative specification: docs/VISUAL_DYNAMICS_V1.md
 *
 * Every level is evaluated in closed form from the event coordinate. There is
 * no frame-to-frame integration anywhere in this file, which is what makes seek
 * exact and the response independent of frame rate.
 */

#include "odm_visual_dynamics.h"

#include "odm_wire.h"

#include <string.h>

#define VD_Q ODM_VD_Q31_ONE
#define VD_POLICY_MAGIC "ODMVDYN1"

/* Canonical timeline. Kept local so this module never depends on render or
 * composition headers. */
#define VD_SAMPLE_RATE UINT64_C(48000)

static uint64_t vd_ms(uint64_t milliseconds) {
    return (milliseconds * VD_SAMPLE_RATE) / UINT64_C(1000);
}

/* ---- Q1.31 primitives, bit-identical to the composition layer ---- */

static uint32_t vd_sat(uint64_t v) {
    return v > (uint64_t)VD_Q ? VD_Q : (uint32_t)v;
}

static uint32_t vd_mul(uint32_t a, uint32_t b) {
    uint64_t p = (uint64_t)a * (uint64_t)b;
    p += (uint64_t)(VD_Q / 2u);
    p /= (uint64_t)VD_Q;
    return vd_sat(p);
}

/* Smoothstep 3t^2-2t^3 with a hard dead zone at or below `lo` and exact unity
 * at or above `hi`. Same curve family as Radial Morphology v2 by design. */
static uint32_t vd_knee(uint32_t value, uint32_t lo, uint32_t hi) {
    uint32_t t, t2, t3;
    uint64_t a, b, r;
    if (value <= lo) return 0u;
    if (value >= hi || hi <= lo) return VD_Q;
    t = (uint32_t)((((uint64_t)(value - lo) * (uint64_t)VD_Q) +
                    (uint64_t)(hi - lo) / 2u) / (uint64_t)(hi - lo));
    t2 = vd_mul(t, t);
    t3 = vd_mul(t2, t);
    a = (uint64_t)t2 * UINT64_C(3);
    b = (uint64_t)t3 * UINT64_C(2);
    r = a > b ? a - b : 0u;
    return vd_sat(r);
}

/* Frozen Q1.31 table of 2^(-i/256), i in [0,256]. Generated once from the
 * closed form and checked by the independent oracle; never recomputed at run
 * time so the release curve is bit-identical on every platform. */
static const uint32_t vd_exp2_neg_table[257] = {
2147483647u,2141676972u,2135885997u,2130110681u,2124350981u,2118606856u,2112878261u,2107165157u,
2101467501u,2095785250u,2090118365u,2084466802u,2078830521u,2073209479u,2067603637u,2062012953u,
2056437386u,2050876894u,2045331438u,2039800977u,2034285469u,2028784875u,2023299155u,2017828267u,
2012372173u,2006930832u,2001504203u,1996092248u,1990694926u,1985312199u,1979944026u,1974590369u,
1969251187u,1963926442u,1958616095u,1953320107u,1948038439u,1942771053u,1937517909u,1932278969u,
1927054195u,1921843548u,1916646991u,1911464485u,1906295992u,1901141475u,1896000895u,1890874215u,
1885761397u,1880662404u,1875577198u,1870505743u,1865448000u,1860403934u,1855373506u,1850356680u,
1845353419u,1840363687u,1835387447u,1830424662u,1825475296u,1820539314u,1815616677u,1810707352u,
1805811301u,1800928488u,1796058878u,1791202436u,1786359125u,1781528910u,1776711756u,1771907627u,
1767116488u,1762338304u,1757573040u,1752820661u,1748081133u,1743354419u,1738640487u,1733939300u,
1729250826u,1724575029u,1719911874u,1715261329u,1710623359u,1705997929u,1701385006u,1696784557u,
1692196546u,1687620942u,1683057709u,1678506816u,1673968228u,1669441911u,1664927834u,1660425963u,
1655936264u,1651458705u,1646993253u,1642539876u,1638098540u,1633669214u,1629251864u,1624846458u,
1620452964u,1616071351u,1611701584u,1607343634u,1602997466u,1598663051u,1594340356u,1590029349u,
1585729999u,1581442274u,1577166143u,1572901574u,1568648536u,1564406999u,1560176930u,1555958299u,
1551751075u,1547555227u,1543370725u,1539197537u,1535035633u,1530884983u,1526745555u,1522617321u,
1518500249u,1514394310u,1510299472u,1506215707u,1502142985u,1498081274u,1494030547u,1489990772u,
1485961920u,1481943963u,1477936869u,1473940611u,1469955158u,1465980482u,1462016553u,1458063342u,
1454120820u,1450188959u,1446267730u,1442357103u,1438457050u,1434567543u,1430688553u,1426820051u,
1422962010u,1419114400u,1415277195u,1411450364u,1407633882u,1403827719u,1400031847u,1396246240u,
1392470868u,1388705705u,1384950723u,1381205894u,1377471190u,1373746586u,1370032052u,1366327562u,
1362633089u,1358948605u,1355274085u,1351609500u,1347954823u,1344310029u,1340675090u,1337049980u,
1333434672u,1329829139u,1326233356u,1322647295u,1319070931u,1315504237u,1311947187u,1308399756u,
1304861916u,1301333643u,1297814910u,1294305691u,1290805961u,1287315694u,1283834865u,1280363447u,
1276901416u,1273448746u,1270005412u,1266571389u,1263146651u,1259731173u,1256324931u,1252927899u,
1249540052u,1246161366u,1242791815u,1239431376u,1236080023u,1232737732u,1229404478u,1226080237u,
1222764985u,1219458697u,1216161349u,1212872917u,1209593377u,1206322704u,1203060875u,1199807866u,
1196563653u,1193328212u,1190101520u,1186883552u,1183674285u,1180473696u,1177281762u,1174098458u,
1170923761u,1167757649u,1164600098u,1161451085u,1158310586u,1155178579u,1152055041u,1148939949u,
1145833280u,1142735011u,1139645119u,1136563583u,1133490379u,1130425484u,1127368877u,1124320535u,
1121280435u,1118248556u,1115224875u,1112209369u,1109202017u,1106202797u,1103211687u,1100228664u,
1097253708u,1094286795u,1091327905u,1088377016u,1085434105u,1082499152u,1079572135u,1076653033u,
1073741824u
};

/* 2^(-numerator/denominator) in Q1.31, for numerator < denominator.
 * Table lookup on the high bits with exact linear interpolation on the rest. */
static uint32_t vd_exp2_neg_frac(uint64_t numerator, uint64_t denominator) {
    uint64_t scaled, index, frac, lo, hi;
    if (denominator == 0u || numerator == 0u) return VD_Q;
    if (numerator >= denominator) return vd_exp2_neg_table[256];
    /* scaled = numerator/denominator in units of 1/256, with the remainder kept
     * separately so interpolation stays exact in integers. */
    scaled = (numerator * UINT64_C(256)) / denominator;
    frac = (numerator * UINT64_C(256)) % denominator;
    index = scaled;
    if (index >= 256u) return vd_exp2_neg_table[256];
    lo = vd_exp2_neg_table[index];
    hi = vd_exp2_neg_table[index + 1u];
    /* lo >= hi: the table is strictly decreasing. */
    return (uint32_t)(lo - ((lo - hi) * frac + denominator / 2u) / denominator);
}

/* peak * 2^(-elapsed/halflife), monotone non-increasing in `elapsed`. */
static uint32_t vd_release(uint32_t peak, uint64_t elapsed, uint64_t halflife) {
    uint64_t whole, frac_num;
    uint32_t v;
    if (halflife == 0u) return 0u;
    whole = elapsed / halflife;
    frac_num = elapsed % halflife;
    if (whole >= 32u) return 0u; /* 2^-32 of full scale is below one Q1.31 ULP */
    v = (uint32_t)((uint64_t)peak >> whole);
    return vd_mul(v, vd_exp2_neg_frac(frac_num, halflife));
}

/* ---- Class table (docs/VISUAL_DYNAMICS_V1.md section 5) ---- */

typedef struct {
    uint64_t attack_ms;
    uint64_t overshoot_ms;
    uint64_t hold_ms;
    uint64_t release_halflife_ms;
    uint64_t refractory_ms;
    uint32_t dead_zone_q31;
    uint32_t knee_hi_q31;
    uint32_t overshoot_q31;
} vd_class_def;

/* Release half-lives are strictly increasing across this table. That ordering
 * is the micro/meso/macro guarantee: a 10 ms transient cannot restructure the
 * scene, and a structural change cannot be drawn as a one-frame flash. */
static const vd_class_def vd_classes[ODM_VD_CLASS_COUNT] = {
    /* RADIAL_TIP     */ {  1u,  0u,  6u,   55u, 20u, UINT32_C(64424509),  UINT32_C(536870912), 0u },
    /* CORE_IMPACT    */ {  4u,  6u, 12u,   90u, 60u, UINT32_C(107374182), UINT32_C(644245094), UINT32_C(128849018) },
    /* HALO_BODY      */ { 25u,  0u, 40u,  180u,  0u, UINT32_C(64424509),  UINT32_C(429496729), 0u },
    /* PARTICLE_SPAWN */ {  0u,  0u,  0u,  300u, 40u, UINT32_C(171798692), UINT32_C(751619277), 0u },
    /* MEMORY_FIELD   */ {  0u,  0u,  0u, 1200u,  0u, UINT32_C(171798692), UINT32_C(858993459), 0u },
    /* GRID_FIELD     */ {180u,  0u,  0u,  700u,  0u, UINT32_C(107374182), UINT32_C(644245094), 0u },
    /* BACKGROUND     */ {600u,  0u,  0u, 2500u,  0u, UINT32_C(107374182), UINT32_C(858993459), 0u }
};

odm_status odm_vd_kernel_class(uint32_t response_class, odm_vd_kernel *out_kernel) {
    const vd_class_def *d;
    odm_vd_kernel k;
    if (!out_kernel || response_class >= (uint32_t)ODM_VD_CLASS_COUNT)
        return ODM_STATUS_INVALID_ARGUMENT;
    d = &vd_classes[response_class];
    memset(&k, 0, sizeof(k));
    k.schema_version = ODM_VISUAL_DYNAMICS_SCHEMA_VERSION;
    k.attack_samples = vd_ms(d->attack_ms);
    k.overshoot_samples = vd_ms(d->overshoot_ms);
    k.hold_samples = vd_ms(d->hold_ms);
    k.release_halflife_samples = vd_ms(d->release_halflife_ms);
    k.refractory_samples = vd_ms(d->refractory_ms);
    k.dead_zone_q31 = d->dead_zone_q31;
    k.knee_hi_q31 = d->knee_hi_q31;
    k.gain_q31 = VD_Q;
    k.max_q31 = VD_Q;
    k.overshoot_q31 = d->overshoot_q31;
    k.floor_q31 = 0u;
    *out_kernel = k;
    return ODM_STATUS_OK;
}

odm_status odm_vd_kernel_validate(const odm_vd_kernel *kernel) {
    if (!kernel) return ODM_STATUS_INVALID_ARGUMENT;
    if (kernel->schema_version != ODM_VISUAL_DYNAMICS_SCHEMA_VERSION ||
        kernel->reserved0 != 0u)
        return ODM_STATUS_INVALID_DATA;
    if (kernel->dead_zone_q31 > VD_Q || kernel->knee_hi_q31 > VD_Q ||
        kernel->gain_q31 > VD_Q || kernel->max_q31 > VD_Q ||
        kernel->overshoot_q31 > VD_Q || kernel->floor_q31 > VD_Q)
        return ODM_STATUS_INVALID_DATA;
    if (kernel->knee_hi_q31 <= kernel->dead_zone_q31) return ODM_STATUS_INVALID_DATA;
    if (kernel->floor_q31 > kernel->max_q31) return ODM_STATUS_INVALID_DATA;
    /* Bounded durations: a response may not outlive a plausible project. */
    if (kernel->attack_samples > VD_SAMPLE_RATE * UINT64_C(60) ||
        kernel->overshoot_samples > VD_SAMPLE_RATE * UINT64_C(60) ||
        kernel->hold_samples > VD_SAMPLE_RATE * UINT64_C(60) ||
        kernel->release_halflife_samples > VD_SAMPLE_RATE * UINT64_C(60) ||
        kernel->refractory_samples > VD_SAMPLE_RATE * UINT64_C(60))
        return ODM_STATUS_INVALID_DATA;
    return ODM_STATUS_OK;
}

odm_status odm_vd_state_init(const odm_vd_kernel *kernel, uint64_t sample,
                             odm_vd_state *out_state) {
    odm_status st;
    if (!out_state) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_vd_kernel_validate(kernel);
    if (st != ODM_STATUS_OK) return st;
    memset(out_state, 0, sizeof(*out_state));
    out_state->event_sample = sample;
    /* Resting: both endpoints are the floor, so every phase evaluates to floor
     * until an event is accepted. This is guarantee G-VD-1. */
    out_state->peak_q31 = kernel->floor_q31;
    out_state->base_q31 = kernel->floor_q31;
    out_state->trigger_sample = 0u;
    return ODM_STATUS_OK;
}

odm_status odm_vd_admit(const odm_vd_kernel *kernel, uint32_t evidence_q31,
                        uint32_t *out_peak_q31) {
    odm_status st;
    uint32_t gate, gated, peak;
    if (!out_peak_q31) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_vd_kernel_validate(kernel);
    if (st != ODM_STATUS_OK) return st;
    if (evidence_q31 > VD_Q) return ODM_STATUS_INVALID_DATA;
    gate = vd_knee(evidence_q31, kernel->dead_zone_q31, kernel->knee_hi_q31);
    gated = vd_mul(evidence_q31, gate);
    peak = vd_mul(gated, kernel->gain_q31);
    if (peak > kernel->max_q31) peak = kernel->max_q31;
    if (peak < kernel->floor_q31) peak = kernel->floor_q31;
    *out_peak_q31 = peak;
    return ODM_STATUS_OK;
}

odm_status odm_vd_eval(const odm_vd_kernel *kernel, const odm_vd_state *state,
                       uint64_t sample, uint32_t *out_level_q31) {
    odm_status st;
    uint64_t d, a, o, h;
    uint32_t level;
    if (!state || !out_level_q31) return ODM_STATUS_INVALID_ARGUMENT;
    st = odm_vd_kernel_validate(kernel);
    if (st != ODM_STATUS_OK) return st;
    if (state->peak_q31 > VD_Q || state->base_q31 > VD_Q)
        return ODM_STATUS_INVALID_DATA;

    if (sample <= state->event_sample) {
        *out_level_q31 = state->base_q31;
        return ODM_STATUS_OK;
    }
    d = sample - state->event_sample;
    a = kernel->attack_samples;
    o = kernel->overshoot_samples;
    h = kernel->hold_samples;

    if (d < a) {
        /* Attack: smoothstep from the level we departed from to the peak. The
         * curve is applied to the interval, not to the value, so an attack that
         * starts mid-release is still continuous. */
        uint32_t t = (uint32_t)(((uint64_t)d * (uint64_t)VD_Q + a / 2u) / a);
        uint32_t shaped = vd_knee(t, 0u, VD_Q);
        if (state->peak_q31 >= state->base_q31) {
            uint32_t span = state->peak_q31 - state->base_q31;
            level = vd_sat((uint64_t)state->base_q31 + vd_mul(span, shaped));
        } else {
            uint32_t span = state->base_q31 - state->peak_q31;
            uint32_t drop = vd_mul(span, shaped);
            level = state->base_q31 > drop ? state->base_q31 - drop : 0u;
        }
    } else if (o != 0u && d < a + o) {
        /* Overshoot: one smooth excursion above peak, hump(x)=4x(1-x), which
         * returns exactly to peak at x=1 so it can never leave an offset. */
        uint64_t e = d - a;
        uint32_t x = (uint32_t)((e * (uint64_t)VD_Q + o / 2u) / o);
        uint32_t hump = vd_sat((uint64_t)vd_mul(x, VD_Q - x) * UINT64_C(4));
        uint32_t bump = vd_mul(vd_mul(state->peak_q31, kernel->overshoot_q31), hump);
        level = vd_sat((uint64_t)state->peak_q31 + bump);
    } else if (d < a + o + h) {
        level = state->peak_q31;
    } else {
        uint64_t elapsed = d - a - o - h;
        if (state->peak_q31 <= kernel->floor_q31) {
            level = kernel->floor_q31;
        } else {
            uint32_t span = state->peak_q31 - kernel->floor_q31;
            level = vd_sat((uint64_t)kernel->floor_q31 +
                           vd_release(span, elapsed, kernel->release_halflife_samples));
        }
    }
    if (level < kernel->floor_q31) level = kernel->floor_q31;
    *out_level_q31 = level;
    return ODM_STATUS_OK;
}

odm_status odm_vd_trigger(const odm_vd_kernel *kernel, odm_vd_state *state,
                          uint64_t sample, uint32_t evidence_q31,
                          uint32_t *out_accepted) {
    odm_status st;
    uint32_t peak = 0u, current = 0u;
    int within_refractory;
    if (!state) return ODM_STATUS_INVALID_ARGUMENT;
    if (out_accepted) *out_accepted = 0u;
    st = odm_vd_admit(kernel, evidence_q31, &peak);
    if (st != ODM_STATUS_OK) return st;
    st = odm_vd_eval(kernel, state, sample, &current);
    if (st != ODM_STATUS_OK) return st;
    if (sample < state->event_sample) return ODM_STATUS_INVALID_DATA;

    /* A trigger must actually add something. */
    if (peak <= current) return ODM_STATUS_OK;

    /* Refractory bounds repetition, not dynamics: a genuinely stronger event is
     * never suppressed by a weaker recent one. */
    within_refractory = kernel->refractory_samples != 0u &&
                        sample >= state->trigger_sample &&
                        (sample - state->trigger_sample) < kernel->refractory_samples;
    if (within_refractory && peak <= state->peak_q31) return ODM_STATUS_OK;

    state->base_q31 = current; /* continuity: start from where we actually are */
    state->peak_q31 = peak;
    state->event_sample = sample;
    state->trigger_sample = sample;
    if (out_accepted) *out_accepted = 1u;
    return ODM_STATUS_OK;
}

odm_status odm_vd_policy_bytes(uint8_t *buffer, uint64_t capacity,
                               uint64_t *out_required) {
    odm_wire_writer w = ODM_WIRE_WRITER_INITIALIZER;
    odm_status st;
    uint32_t i;
    if (!out_required) return ODM_STATUS_INVALID_ARGUMENT;
    *out_required = ODM_VISUAL_DYNAMICS_POLICY_BYTES;
    if (!buffer || capacity < ODM_VISUAL_DYNAMICS_POLICY_BYTES)
        return ODM_STATUS_BUFFER_TOO_SMALL;
    memset(buffer, 0, ODM_VISUAL_DYNAMICS_POLICY_BYTES);
    st = odm_wire_writer_init(&w, buffer, ODM_VISUAL_DYNAMICS_POLICY_BYTES);
    if (st != ODM_STATUS_OK) return st;
#define VD_W(call) do { st = (call); if (st != ODM_STATUS_OK) return st; } while (0)
    VD_W(odm_wire_write_bytes(&w, VD_POLICY_MAGIC, 8u));
    VD_W(odm_wire_write_u32(&w, ODM_VISUAL_DYNAMICS_POLICY_VERSION));
    VD_W(odm_wire_write_u32(&w, ODM_VISUAL_DYNAMICS_SCHEMA_VERSION));
    VD_W(odm_wire_write_u32(&w, VD_Q));
    VD_W(odm_wire_write_u64(&w, VD_SAMPLE_RATE));
    VD_W(odm_wire_write_u32(&w, (uint32_t)ODM_VD_CLASS_COUNT));
    VD_W(odm_wire_write_u32(&w, 1u)); /* durations are sample counts, not frames or ticks */
    VD_W(odm_wire_write_u32(&w, 1u)); /* response is evaluated in closed form from the event */
    VD_W(odm_wire_write_u32(&w, 1u)); /* no accepted trigger => level is exactly floor */
    VD_W(odm_wire_write_u32(&w, 1u)); /* retrigger departs from the current level */
    VD_W(odm_wire_write_u32(&w, 1u)); /* refractory bounds repetition, stronger event wins */
    VD_W(odm_wire_write_u32(&w, 1u)); /* admission knee is smoothstep 3t^2-2t^3 */
    VD_W(odm_wire_write_u32(&w, 2u)); /* release curve id: 2 = exponential half-life */
    VD_W(odm_wire_write_u32(&w, 256u)); /* release fraction table resolution */
    VD_W(odm_wire_write_u32(&w, 32u)); /* release truncation in half-lives */
    VD_W(odm_wire_write_u32(&w, 3u)); /* overshoot curve id: 3 = 4x(1-x) hump */
    for (i = 0u; i < (uint32_t)ODM_VD_CLASS_COUNT; ++i) {
        odm_vd_kernel k;
        st = odm_vd_kernel_class(i, &k);
        if (st != ODM_STATUS_OK) return st;
        VD_W(odm_wire_write_u32(&w, i));
        VD_W(odm_wire_write_u64(&w, k.attack_samples));
        VD_W(odm_wire_write_u64(&w, k.overshoot_samples));
        VD_W(odm_wire_write_u64(&w, k.hold_samples));
        VD_W(odm_wire_write_u64(&w, k.release_halflife_samples));
        VD_W(odm_wire_write_u64(&w, k.refractory_samples));
        VD_W(odm_wire_write_u32(&w, k.dead_zone_q31));
        VD_W(odm_wire_write_u32(&w, k.knee_hi_q31));
        VD_W(odm_wire_write_u32(&w, k.gain_q31));
        VD_W(odm_wire_write_u32(&w, k.max_q31));
        VD_W(odm_wire_write_u32(&w, k.overshoot_q31));
        VD_W(odm_wire_write_u32(&w, k.floor_q31));
    }
#undef VD_W
    return odm_wire_writer_finish(&w, out_required);
}
