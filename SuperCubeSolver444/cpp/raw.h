// Raw-state helpers: solution paths, scramble/solution replay on flat arrays,
// whole-cube-rotation conjugation, and State6 extraction.
//
// A search path is a vector<int>: entries < NUM_MOVES are moves; the sentinels
// below encode whole-cube rotations (free, not counted as moves) interleaved at
// the exact point they occur: ROTATION_SENTINEL_BASE+cls closes a rotation
// (also emitted for a rotation forced by canonicalization), and
// P2_OPEN_SENTINEL_BASE+cls opens the conjugated frame of a fork branch.
// Classes 0..5 are the S1 classes (axis-pair permutations), classes 6..8 the whole-cube half turns x2, y2, z2
// that canonicalize a state that is S4 only up to such a half turn (S3'->S4 crossing, see state.h).
#pragma once
#include "state.h"

static const int ROTATION_SENTINEL_BASE = NUM_MOVES;
static const int P2_OPEN_SENTINEL_BASE = ROTATION_SENTINEL_BASE + NUM_ROTATE_CLASSES;
// NISS tokens (search paths only; assemble_niss removes them from the final solution):
//   NISS_FLIP_TOKEN          apply the pending free move (see niss.h) to the current cube state
//   NISS_SWITCH_BASE         invert the cube state; NISS_SWITCH_BASE + 1 + m additionally creates a
//                            pending free move m^2 (m = the quarter turn that just entered the tier)
static const int NISS_FLIP_TOKEN = P2_OPEN_SENTINEL_BASE + NUM_ROTATE_CLASSES;
static const int NISS_SWITCH_BASE = NISS_FLIP_TOKEN + 1;
static inline bool is_niss_token(int e) { return e >= NISS_FLIP_TOKEN; }

// Conjugate a state by whole-cube rotation class `cls` (see rotations.h):
// re-labels the SAME physical arrangement under a rotated reference frame.
// corner_parity is a quarter-turn count, unaffected by relabeling.
static inline State6 conjugate_state6(const State6& ns, int cls) {
    State6 conj = ns;
    for (int i = 0; i < 24; i++) conj.center_slot[i] = ROTATE_CENTER[cls][ns.center_slot[ROTATE_CENTER_INV[cls][i]]];
    for (int i = 0; i < 24; i++) conj.wing_slot[i] = ROTATE_WING[cls][ns.wing_slot[ROTATE_WING_INV[cls][i]]];
    for (int i = 0; i < 24; i++) conj.corner_sticker[i] = ROTATE_CORNER[cls][ns.corner_sticker[ROTATE_CORNER_INV[cls][i]]];
    conj.s1 = p1::extract(conj.wing_slot.data(), conj.center_slot.data());
    conj.wing = p2full::wing_mask_of(conj.wing_slot);
    return conj;
}

static void apply_raw_move(int* wing_slot, int* center_slot, int* corner_parity, int* corner_sticker, int m) {
    int nw[24], nc[24], ncorner[24];
    for (int i=0;i<24;i++) nw[i] = WING_PERM[m][wing_slot[i]];
    for (int i=0;i<24;i++) nc[i] = CENTER_PERM[m][center_slot[i]];
    for (int i=0;i<24;i++) ncorner[i] = CORNER_PERM[m][corner_sticker[i]];
    for (int i=0;i<24;i++) { wing_slot[i]=nw[i]; center_slot[i]=nc[i]; corner_sticker[i]=ncorner[i]; }
    *corner_parity ^= (coords::IS_QUARTER_TURN[m] ? 1 : 0);
}

static void apply_raw_rotation_close(int* wing_slot, int* center_slot, int* corner_sticker, int cls) {
    int nw[24], nc[24], ncorner[24];
    for (int i=0;i<24;i++) nw[i] = ROTATE_WING_INV[cls][wing_slot[i]];
    for (int i=0;i<24;i++) nc[i] = ROTATE_CENTER_INV[cls][center_slot[i]];
    for (int i=0;i<24;i++) ncorner[i] = ROTATE_CORNER_INV[cls][corner_sticker[i]];
    for (int i=0;i<24;i++) { wing_slot[i]=nw[i]; center_slot[i]=nc[i]; corner_sticker[i]=ncorner[i]; }
}

static void apply_raw_rotation_open(int* wing_slot, int* center_slot, int* corner_sticker, int cls) {
    int nw[24], nc[24], ncorner[24];
    for (int i=0;i<24;i++) nw[i] = ROTATE_WING[cls][wing_slot[i]];
    for (int i=0;i<24;i++) nc[i] = ROTATE_CENTER[cls][center_slot[i]];
    for (int i=0;i<24;i++) ncorner[i] = ROTATE_CORNER[cls][corner_sticker[i]];
    for (int i=0;i<24;i++) { wing_slot[i]=nw[i]; center_slot[i]=nc[i]; corner_sticker[i]=ncorner[i]; }
}

static inline bool is_rotation_entry(int e) { return e >= ROTATION_SENTINEL_BASE; }
static inline bool is_open_rotation_entry(int e) { return e >= P2_OPEN_SENTINEL_BASE && e < NISS_FLIP_TOKEN; }
static inline int rotation_class_of(int e) {
    return is_open_rotation_entry(e) ? (e - P2_OPEN_SENTINEL_BASE) : (e - ROTATION_SENTINEL_BASE);
}

static int solution_move_count(const std::vector<int>& path) {
    int n = 0;
    for (int e : path) if (!is_rotation_entry(e)) n++;
    return n;
}

static void apply_solution(int* wing_slot, int* center_slot, int* corner_parity, int* corner_sticker, const std::vector<int>& path) {
    for (int e : path) {
        if (is_open_rotation_entry(e)) apply_raw_rotation_open(wing_slot, center_slot, corner_sticker, rotation_class_of(e));
        else if (is_rotation_entry(e)) apply_raw_rotation_close(wing_slot, center_slot, corner_sticker, rotation_class_of(e));
        else apply_raw_move(wing_slot, center_slot, corner_parity, corner_sticker, e);
    }
}

static std::string format_solution(const std::vector<int>& path) {
    std::string out;
    for (int e : path) {
        if (!out.empty()) out += " ";
        if (is_open_rotation_entry(e)) out += std::string("[") + ROTATE_CLASS_FWD_NOTATION[rotation_class_of(e)] + "]";
        else if (is_rotation_entry(e)) out += std::string("[") + ROTATE_CLASS_INV_NOTATION[rotation_class_of(e)] + "]";
        else out += MOVE_NAMES[e];
    }
    return out;
}

static State6 extract6(const int* wing_slot, const int* center_slot, int corner_parity,
                        const int* corner_sticker, int* out_root_rotation = nullptr) {
    State6 s;
    s.s1 = p1::extract(wing_slot, center_slot);
    s.wing = p2full::wing_mask_of(wing_slot);
    for (int i=0;i<24;i++) s.center_slot[i] = center_slot[i];
    for (int i=0;i<24;i++) s.wing_slot[i] = wing_slot[i];
    s.corner_parity = corner_parity;
    for (int i=0;i<24;i++) s.corner_sticker[i] = corner_sticker[i];
    int cls = canonicalize_if_rotated(s, true);
    if (out_root_rotation) *out_root_rotation = cls;
    return s;
}

// A cube as flat piece->slot arrays (the scramble / solution replay model).
struct Cube {
    int wing[24], center[24], corner[24], corner_parity;
    Cube() { reset(); }
    void reset() { for (int i = 0; i < 24; i++) { wing[i] = i; center[i] = i; corner[i] = i; } corner_parity = 0; }
    void apply(int m) { apply_raw_move(wing, center, &corner_parity, corner, m); }
    State6 state(int* root_rot = nullptr) const { return extract6(wing, center, corner_parity, corner, root_rot); }
};
static void apply_letters(Cube& c, const std::vector<int>& path) { apply_solution(c.wing, c.center, &c.corner_parity, c.corner, path); }
