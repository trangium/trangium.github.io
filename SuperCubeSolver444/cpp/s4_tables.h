// S3' -> S4 heuristic tables (chosen over the 406M-entry alternative, see ../archive/cpp/variant_b_big_tables).
//
//   T1 = layer x ud                      (20,160 x 2,520 = 50,803,200 states)
//   T2 = (eq x pll x lr x fb) x ud       (4,608  x 2,520 = 11,612,160 states)
//   h  = max(T1, T2)
//
// The tier-3 lazy state tracks three coordinates: layer (20,160), ud (2,520)
// and rest = eq x pll x lr x fb (4,608); each table is then a lookup over two
// of them. Replacing T1 by layer x eq x pll x ud (406M entries) cut nodes by
// under 1% but cost far more in cache misses, so it was archived.
// The tables are stored symmetry-reduced (about 20x smaller): see s4_sym.h.
#pragma once
#include "s4_coords.h"

namespace coords {

static const char* S4_VARIANT_NAME = "layer x ud (50.8M) + eq x pll x lr x fb x ud (11.6M), distance to S4 mod half turns, symmetry-reduced";

static Coord C_REST;  // eq x pll x lr x fb, id = ((eq*2+pll)*24+lr)*24+fb

// S4 is defined up to the whole-cube half turns x2, y2, z2 (a state that is S4 after one of them is canonicalized by it,
// like a rotated S1). Goal 0 is the literal S4, goals 1..3 are its images under x2, y2, z2: the dense ud id and the
// (pll = 0) rest id of each (the layer, eq and pll coordinates are all 0 there). Filled by init_s4_goals() (state.h).
static int S4_GOAL_UD[4] = {0, 0, 0, 0}, S4_GOAL_REST[4] = {0, 0, 0, 0};

static void build_rest_coord() { C_REST = compose(compose(compose(C_EQ, C_PLL), C_LR), C_FB); }

// Lazy tier-3 (in S3', not yet S4) state. All-zero is literal S4 up to PLL parity.
struct Tier3State { int32_t layer; int16_t ud; int16_t rest; };

static inline Tier3State tier3_from_ids(const S4Ids& id) {
    return Tier3State{id.layer, (int16_t)id.ud, (int16_t)(((id.eq * 2 + id.pll) * 24 + id.lr) * 24 + id.fb)};
}
static inline Tier3State tier3_apply(const Tier3State& t, int k) {
    return Tier3State{C_LAYER.step(t.layer, k), (int16_t)C_UD.step(t.ud, k), (int16_t)C_REST.step(t.rest, k)};
}
// S4 up to a whole-cube half turn (pairing, centers) -- PLL parity is deliberately ignored, as in is_in_s4(State6).
// Returns -1 (not S4), 0 (literal S4) or k = 1, 2, 3: S4 after the half turn x2, y2, z2 (rotation class 5 + k).
// rest = ((eq*2 + pll)*24 + lr)*24 + fb, so eq == 0 is rest < 1152 and the pll bit is the +576.
static inline int tier3_s4_class(const Tier3State& t) {
    if (t.layer != 0 || t.rest >= 1152) return -1;
    const int base = t.rest >= 576 ? t.rest - 576 : t.rest;
    for (int k = 0; k < 4; k++)
        if (t.ud == S4_GOAL_UD[k] && base == S4_GOAL_REST[k]) return k;
    return -1;
}
static inline bool tier3_is_s4(const Tier3State& t) { return tier3_s4_class(t) >= 0; }

} // namespace coords
