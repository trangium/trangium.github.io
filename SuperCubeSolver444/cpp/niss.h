// NISS (normal/inverse scramble switching): state algebra and solution assembly.
//
// Model. The search always holds ONE cube state X and appends moves on its right.
// A switch replaces X by X^-1 (free), which flips the search between working on
// the normal scramble and on the inverse scramble. Writing N / I for the move
// words appended while on the normal / inverse side, the invariant is
//     V = N^-1 S^-1 I,      X = V on the inverse side, X = V^-1 on the normal side,
// and V = identity  <=>  S N I^-1 = identity, so the final solution is
//     N followed by the inverse of I.
// A whole-cube-rotation fork conjugates X exactly as before; it inserts the
// rotation letter at the end of BOTH words (so the closing rotation falls out of
// "inverse of I" and no explicit close is ever emitted in NISS mode).
//
// Free moves. If a segment ended with the quarter turn m that just entered a
// tier, the path with m^-1 in its place enters the same tier. After a switch the
// alternative cube is m^2 * X (a LEFT multiplication, "the free move on the other
// side"); the alternative is realised by flipping that letter m -> m^-1 in the
// final word. The free move is a Pending: it is conjugated by every later
// rotation fork, and it is resolved (a FLIP branch and a no-flip branch) when
//   - the search switches back (FLIP then SWITCH: (g X)^-1 = X^-1 g), or
//   - a tier is reached whose target is not invariant under g * (wide free
//     moves: reaching S2; non-wide ones: reaching S6), or
// otherwise it just rides along. See search.h for where the branches are made.
#pragma once
#include "raw.h"

static bool g_niss = true;   // on by default; --no-niss gives the plain search

static inline int inverse_move(int m) { int p = m % 3; return p == 1 ? m : (m / 3) * 3 + (p == 0 ? 2 : 0); }
static inline bool is_wide_family(int m) { int f = m / 3; return f == 1 || f == 4 || f == 7; }  // Uw, Rw, Fw

// ---------------------------------------------------------------- state algebra
static bool same_state(const State6& a, const State6& b) {
    return a.center_slot == b.center_slot && a.wing_slot == b.wing_slot && a.corner_sticker == b.corner_sticker &&
           a.s1 == b.s1 && a.wing == b.wing && a.corner_parity == b.corner_parity;
}

static inline void refresh_derived(State6& r) {
    r.s1 = p1::extract(r.wing_slot.data(), r.center_slot.data());
    r.wing = p2full::wing_mask_of(r.wing_slot);
}

// X^-1 as a cube state. Like apply_move it canonicalizes a state that is S1 up to
// a rotation (only ever happens for the root scramble); *out_rot is that class or -1.
static inline State6 invert_state6(const State6& s, int* out_rot = nullptr) {
    State6 r = s;
    for (int i = 0; i < 24; i++) { r.center_slot[s.center_slot[i]] = i; r.wing_slot[s.wing_slot[i]] = i; r.corner_sticker[s.corner_sticker[i]] = i; }
    refresh_derived(r);
    int cls = canonicalize_if_rotated(r, true);
    if (out_rot) *out_rot = cls;
    return r;
}

// The free move as a cube element (piece -> position arrays, same convention as
// State6's arrays), tracked in the current rotation frame.
struct Pending {
    bool active = false;
    bool wide = false;       // wide free moves are resolved on reaching S2, face half turns on reaching S6
    int face_half = -1;      // raw move index of the face half turn (non-wide only; follows rotation forks)
    std::array<int,24> c, w, k;
};

static inline Pending make_pending(int m) {   // m = a quarter turn; the free move is its square
    Pending p;
    int half = (m / 3) * 3 + 1;
    p.active = true;
    p.wide = is_wide_family(m);
    p.face_half = p.wide ? -1 : half;
    for (int i = 0; i < 24; i++) { p.c[i] = CENTER_PERM[half][i]; p.w[i] = WING_PERM[half][i]; p.k[i] = CORNER_PERM[half][i]; }
    return p;
}

// X -> g * X (g applied first).
static inline State6 leftmul_state6(const State6& s, const Pending& g) {
    State6 r = s;
    for (int i = 0; i < 24; i++) { r.center_slot[i] = s.center_slot[g.c[i]]; r.wing_slot[i] = s.wing_slot[g.w[i]]; r.corner_sticker[i] = s.corner_sticker[g.k[i]]; }
    refresh_derived(r);
    return r;
}

static int CONJ_FACE_HALF[NUM_ROTATE_CLASSES][NUM_MOVES];   // conjugating a face half turn by a rotation class gives another face half turn

static inline std::array<int,24> conj_arr(const std::array<int,24>& a, const int (*R)[24], const int (*RI)[24], int cls) {
    std::array<int,24> r;
    for (int i = 0; i < 24; i++) r[i] = R[cls][a[RI[cls][i]]];
    return r;
}
static inline Pending conj_pending(const Pending& p, int cls) {
    Pending r = p;
    r.c = conj_arr(p.c, ROTATE_CENTER, ROTATE_CENTER_INV, cls);
    r.w = conj_arr(p.w, ROTATE_WING, ROTATE_WING_INV, cls);
    r.k = conj_arr(p.k, ROTATE_CORNER, ROTATE_CORNER_INV, cls);
    if (p.face_half >= 0) r.face_half = CONJ_FACE_HALF[cls][p.face_half];
    return r;
}

// ---------------------------------------------------------------- tier-6 left multiplication
// g * X on the lazy Tier6State for a face half turn g. The coordinates are
// quotients on which left multiplication by an element of the S6 group is
// well defined (it only relabels pieces inside their own class); the tables
// are built from one representative per coordinate value and checked against
// full-cube left multiplication in the self-tests.
static int T6_LEFT_CORNER[96][6];
static std::vector<int> T6_LEFT_EC;   // [ec * 6 + t]

static inline Tier6State t6_leftmul(const Tier6State& t, int face_half_move) {
    int j = coords::FULL_TO_TIER6_MOVE_INDEX[face_half_move];
    Tier6State r;
    r.corner_h = (int8_t)T6_LEFT_CORNER[t.corner_h][j];
    r.ec = T6_LEFT_EC[(size_t)t.ec * 6 + j];
    return r;
}

static void init_niss_tables() {
    LapTimer lt;
    // conj of face half turns
    static const int FACE_HALVES[6] = {1, 7, 10, 16, 19, 25};
    for (int cls = 0; cls < NUM_ROTATE_CLASSES; cls++)
        for (int m = 0; m < NUM_MOVES; m++) CONJ_FACE_HALF[cls][m] = -1;
    for (int cls = 1; cls < NUM_ROTATE_CLASSES; cls++) {
        for (int h : FACE_HALVES) {
            Pending p = make_pending(h - 1);   // any quarter turn of the same face gives this half turn
            Pending q = conj_pending(p, cls);
            for (int h2 : FACE_HALVES) {
                bool same_el = true;
                for (int i = 0; i < 24 && same_el; i++)
                    same_el = q.c[i] == CENTER_PERM[h2][i] && q.w[i] == WING_PERM[h2][i] && q.k[i] == CORNER_PERM[h2][i];
                if (same_el) CONJ_FACE_HALF[cls][h] = h2;
            }
        }
    }
    for (int m : FACE_HALVES) CONJ_FACE_HALF[0][m] = m;
    lt.lap("conjugation of face half turns");

    if (!g_legacy_tail) return;   // --ls: the left multiplication of the S6/S7 coordinates is not used
    // corner: BFS over H from the identity, one representative sticker array per H rank
    {
        std::vector<std::array<int,24>> rep(96);
        std::vector<bool> seen(96, false);
        std::array<int,24> id; for (int i = 0; i < 24; i++) id[i] = i;
        std::vector<std::array<int,24>> queue = {id};
        seen[coords::h_rank_of(id)] = true; rep[coords::h_rank_of(id)] = id;
        for (size_t qi = 0; qi < queue.size(); qi++) {
            for (int t = 0; t < 6; t++) {
                std::array<int,24> n;
                for (int i = 0; i < 24; i++) n[i] = CORNER_PERM[coords::TIER6_MOVES[t]][queue[qi][i]];
                int r = coords::h_rank_of(n);
                if (!seen[r]) { seen[r] = true; rep[r] = n; queue.push_back(n); }
            }
        }
        for (int h = 0; h < 96; h++)
            for (int j = 0; j < 6; j++) {
                std::array<int,24> n;
                for (int i = 0; i < 24; i++) n[i] = rep[h][CORNER_PERM[coords::TIER6_MOVES[j]][i]];
                T6_LEFT_CORNER[h][j] = coords::h_rank_of(n);
            }
    }
    lt.lap("T6 left multiplication: corners");
    // edges+centers: BFS over the 110,592 reachable coordinates
    {
        struct Rep { std::array<uint8_t,24> w, c; };
        std::vector<Rep> rep(coords::EC_DENSE_NUM);
        std::vector<bool> seen(coords::EC_DENSE_NUM, false);
        std::array<int,24> cs; int ws[24];
        for (int i = 0; i < 24; i++) { cs[i] = i; ws[i] = i; }
        Rep r0; for (int i = 0; i < 24; i++) { r0.w[i] = (uint8_t)i; r0.c[i] = (uint8_t)i; }
        int e0 = coords::ec_dense_of(ws, cs);
        seen[e0] = true; rep[e0] = r0;
        std::vector<Rep> queue = {r0};
        for (size_t qi = 0; qi < queue.size(); qi++) {
            for (int t = 0; t < 6; t++) {
                Rep n; int wn[24]; std::array<int,24> cn;
                for (int i = 0; i < 24; i++) { n.w[i] = (uint8_t)WING_PERM[coords::TIER6_MOVES[t]][queue[qi].w[i]]; n.c[i] = (uint8_t)CENTER_PERM[coords::TIER6_MOVES[t]][queue[qi].c[i]]; wn[i] = n.w[i]; cn[i] = n.c[i]; }
                int e = coords::ec_dense_of(wn, cn);
                if (!seen[e]) { seen[e] = true; rep[e] = n; queue.push_back(n); }
            }
        }
        T6_LEFT_EC.assign((size_t)coords::EC_DENSE_NUM * 6, -1);
        for (int e = 0; e < coords::EC_DENSE_NUM; e++) {
            if (!seen[e]) continue;
            for (int j = 0; j < 6; j++) {
                int wn[24]; std::array<int,24> cn;
                for (int i = 0; i < 24; i++) { wn[i] = rep[e].w[WING_PERM[coords::TIER6_MOVES[j]][i]]; cn[i] = rep[e].c[CENTER_PERM[coords::TIER6_MOVES[j]][i]]; }
                T6_LEFT_EC[(size_t)e * 6 + j] = coords::ec_dense_of(wn, cn);
            }
        }
    }
}

// ---------------------------------------------------------------- solution assembly
static inline int inverse_letter(int e) {
    if (e < NUM_MOVES) return inverse_move(e);
    if (is_open_rotation_entry(e)) return ROTATION_SENTINEL_BASE + rotation_class_of(e);   // rotation -> its inverse
    return P2_OPEN_SENTINEL_BASE + rotation_class_of(e);
}

// Path (chronological moves, rotation letters, NISS tokens) -> the N word followed
// by the inverse of the I word, with every FLIP applied to its letter. If
// `notation` is given it receives the parenthesised shorthand: I-side moves in ().
static std::vector<int> assemble_niss(const std::vector<int>& path, std::string* notation = nullptr) {
    struct Ev { int side; int tok; };
    std::vector<int> w[2];
    std::vector<Ev> events;
    std::vector<int> ev_of[2];   // index into events of each letter of each word
    int side = 0, pw = -1, pi = -1;
    for (int e : path) {
        if (e == NISS_FLIP_TOKEN) {
            if (pw < 0) { printf("BUG: FLIP without a pending free move\n"); abort(); }
            w[pw][pi] = inverse_move(w[pw][pi]);
            events[ev_of[pw][pi]].tok = w[pw][pi];
            pw = -1;
        } else if (e >= NISS_SWITCH_BASE) {
            if (e > NISS_SWITCH_BASE) {
                pw = side; pi = (int)w[side].size() - 1;
                if (pi < 0 || w[side][pi] != e - NISS_SWITCH_BASE - 1) { printf("BUG: pending free move is not the last letter\n"); abort(); }
            } else pw = -1;
            side ^= 1;
        } else if (is_open_rotation_entry(e)) {
            events.push_back({side, e});
            w[side].push_back(e); ev_of[side].push_back((int)events.size() - 1);
            w[side ^ 1].push_back(e); ev_of[side ^ 1].push_back((int)events.size() - 1);
        } else {
            events.push_back({side, e});
            w[side].push_back(e); ev_of[side].push_back((int)events.size() - 1);
        }
    }
    std::vector<int> out = w[0];
    for (int i = (int)w[1].size() - 1; i >= 0; i--) out.push_back(inverse_letter(w[1][i]));
    if (notation) {
        notation->clear();
        int cur = -1;
        for (auto& ev : events) {
            if (ev.side != cur) {
                if (cur == 1) *notation += ")";
                if (!notation->empty()) *notation += " ";
                if (ev.side == 1) *notation += "(";
                cur = ev.side;
            } else *notation += " ";
            if (ev.tok < NUM_MOVES) *notation += MOVE_NAMES[ev.tok];
            else if (is_open_rotation_entry(ev.tok)) *notation += std::string("[") + ROTATE_CLASS_FWD_NOTATION[rotation_class_of(ev.tok)] + "]";
            else *notation += std::string("[") + ROTATE_CLASS_INV_NOTATION[rotation_class_of(ev.tok)] + "]";
        }
        if (cur == 1) *notation += ")";
    }
    return out;
}
