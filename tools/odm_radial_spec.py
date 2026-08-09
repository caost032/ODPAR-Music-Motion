#!/usr/bin/env python3
"""Independent reconstruction of Radial Morphology v2.

Written from docs/RADIAL_MORPHOLOGY_V2.md. No production C source is parsed and
no C output is used to derive any value here. Oracles import this module so the
specification exists exactly once on the checking side; the engine remains the
observed system under test.

Visual Policy v9  -> composition morphology (section 3)
Layered Policy v11 -> field raster optics   (section 4)
"""

from __future__ import annotations

Q = 2147483647

# section 3.1 - frozen knee constants (Q1.31)
KNEE_BODY_LO = 107374182
KNEE_BODY_HI = 429496729
KNEE_ATTACK_LO = 171798692
KNEE_ATTACK_HI = 536870912
KNEE_RELEASE_LO = 64424509
KNEE_RELEASE_HI = 322122547

# section 3.2 - frozen mix weights
BODY_CEILING = 966367642      # 0.45
RELEASE_INFLUENCE = 751619277  # 0.35

# section 4 - frozen raster optics
TAIL_WEIGHT = 1395864371       # 0.65
AUTHORITY_FLOOR = 322122547    # 0.15


def mul(a: int, b: int) -> int:
    """Saturating Q1.31 multiply, round-half-up (spec section 1)."""
    return min(Q, (a * b + Q // 2) // Q)


def add(a: int, b: int) -> int:
    return min(Q, a + b)


def knee(v: int, lo: int, hi: int) -> int:
    """Soft knee: hard dead zone below `lo`, smoothstep to unity at `hi`."""
    if v <= lo:
        return 0
    if v >= hi or hi <= lo:
        return Q
    t = ((v - lo) * Q + (hi - lo) // 2) // (hi - lo)
    t2 = mul(t, t)
    t3 = mul(t2, t)
    r = 3 * t2 - 2 * t3
    return min(Q, max(0, r))


def morphology(slow: int, fast: int, attack_in: int) -> tuple[int, int, int, int]:
    """Composition morphology for one lane (spec section 3.3).

    Returns the published provenance tuple `(final, body, release, attack)`.
    """
    if not 0 <= fast <= slow <= Q:
        raise ValueError(f"lane envelope ordering violated: slow={slow} fast={fast}")
    if not 0 <= attack_in <= Q:
        raise ValueError(f"attack out of range: {attack_in}")

    body_gate = knee(fast, KNEE_BODY_LO, KNEE_BODY_HI)
    attack_gate = knee(attack_in, KNEE_ATTACK_LO, KNEE_ATTACK_HI)
    release_raw = slow - fast
    release_gate = knee(release_raw, KNEE_RELEASE_LO, KNEE_RELEASE_HI)

    body = mul(mul(fast, body_gate), BODY_CEILING)
    release = mul(release_raw, release_gate)
    attack = mul(attack_in, attack_gate)

    release_mix = mul(release, RELEASE_INFLUENCE)
    after_release = add(body, mul(Q - body, release_mix))
    final = add(after_release, mul(Q - after_release, attack))
    return final, body, release, attack


def release_base(body: int, release: int, timescale: bool = True) -> int:
    """Radius authority at which the release segment ends (spec section 4)."""
    if not timescale:
        return body
    return add(body, mul(Q - body, mul(release, RELEASE_INFLUENCE)))


def body_opacity(field_op: int, attack: int, release: int) -> int:
    """Dual-domain optical authority for the held-body segment (spec 4.1)."""
    activity = max(attack, mul(release, TAIL_WEIGHT))
    authority = add(AUTHORITY_FLOOR, mul(Q - AUTHORITY_FLOOR, activity))
    return mul(field_op, authority)


def release_opacity(field_op: int, release: int) -> int:
    """Release segment opacity (spec 4.2, unchanged from v10)."""
    return mul(field_op, release)


def tip_opacity(field_op: int, attack: int) -> int:
    """Attack tip opacity: quadratic in same-lane attack (spec 4.3)."""
    return mul(field_op, mul(attack, attack))


def bar_length(bar_max_q16: int, value_q31: int) -> int:
    """Linear Q16.16 radial length. Geometry is never curved (I-RO-1)."""
    return (bar_max_q16 * value_q31 + Q // 2) // Q
