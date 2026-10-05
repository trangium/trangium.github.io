// Forced prefix / suffix.
//
// To get a solution of the scramble S that starts with the algorithm P and ends with the algorithm L, the
// search solves the cube  L S P  (apply L, then S, then P). If Z solves it, L S P Z = id, and rotating that
// identity cyclically gives S P Z L = L^-1 (L S P Z) L = id: the answer is  P Z L.
// (It is L here, not L^-1: L^-1 S P Z = id would only give S P Z L^-1 = id.)
//
// Z must not cancel with P or with L: a move of Z may not merge into a move of P (or of L into Z), even
// through moves in between that commute with it (P = "U F": Z may not start with F, F', F2, or with "B F").
// This is checked exactly, on the finished solution, at the moment the search reaches the solved cube
// (search.h, goal_accepted): a solved cube whose solution would cancel is simply not accepted and the search
// goes on. The check works on cube elements rather than letters, so it is exact across the whole-cube
// rotations of the solution too: every move is rewritten in the frame the solution started in (a move after
// a rotation r turns the layers r^-1 m r), two moves then merge iff they are powers of each other and
// everything between them commutes with them.
#pragma once
#include "niss.h"

// A cube element as three piece->slot arrays (centers, wings, corner stickers); "a after b" is a[b[i]].
using Elem = std::array<uint8_t, 72>;

static inline Elem elem_identity() { Elem e; for (int blk = 0; blk < 3; blk++) for (int i = 0; i < 24; i++) e[blk * 24 + i] = (uint8_t)i; return e; }
static inline Elem elem_compose(const Elem& a, const Elem& b) {
    Elem c;
    for (int blk = 0; blk < 3; blk++) for (int i = 0; i < 24; i++) c[blk * 24 + i] = a[blk * 24 + b[blk * 24 + i]];
    return c;
}
static inline Elem elem_inverse(const Elem& a) {
    Elem r;
    for (int blk = 0; blk < 3; blk++) for (int i = 0; i < 24; i++) r[blk * 24 + a[blk * 24 + i]] = (uint8_t)i;
    return r;
}
static inline Elem move_elem(int m) {
    Elem e;
    for (int i = 0; i < 24; i++) { e[i] = (uint8_t)CENTER_PERM[m][i]; e[24 + i] = (uint8_t)WING_PERM[m][i]; e[48 + i] = (uint8_t)CORNER_PERM[m][i]; }
    return e;
}
// a rotation letter of a path: P2_OPEN_SENTINEL_BASE+cls is the forward rotation, ROTATION_SENTINEL_BASE+cls its inverse
static inline Elem rotation_elem(int entry) {
    const int cls = rotation_class_of(entry);
    const bool open = is_open_rotation_entry(entry);
    Elem e;
    for (int i = 0; i < 24; i++) {
        e[i] = (uint8_t)(open ? ROTATE_CENTER[cls][i] : ROTATE_CENTER_INV[cls][i]);
        e[24 + i] = (uint8_t)(open ? ROTATE_WING[cls][i] : ROTATE_WING_INV[cls][i]);
        e[48 + i] = (uint8_t)(open ? ROTATE_CORNER[cls][i] : ROTATE_CORNER_INV[cls][i]);
    }
    return e;
}

static bool is_power_of(const Elem& x, const Elem& a) {   // x == a, a^2 or a^3
    const Elem a2 = elem_compose(a, a), a3 = elem_compose(a2, a);
    return x == a || x == a2 || x == a3;
}
static bool same_cyclic_group(const Elem& a, const Elem& b) { return is_power_of(a, b) || is_power_of(b, a); }

// Does any move of `z` merge into a move of `prefix`, or any move of `suffix` into a move of `z`? Rotation
// letters (path entries >= NUM_MOVES) are allowed anywhere in the three words; they are treated as real
// whole-cube rotations in between the moves.
static bool forced_cancels(const std::vector<int>& prefix, const std::vector<int>& z, const std::vector<int>& suffix) {
    struct Mv { Elem e; int seg; };
    std::vector<Mv> mv;
    Elem g = elem_identity(), ginv = elem_identity();   // rotations so far, and their inverse
    auto feed = [&](const std::vector<int>& w, int seg) {
        for (int e : w) {
            if (e < NUM_MOVES) mv.push_back({elem_compose(ginv, elem_compose(move_elem(e), g)), seg});
            else if (e < NISS_FLIP_TOKEN) {
                Elem r = rotation_elem(e);
                g = elem_compose(r, g);
                ginv = elem_compose(ginv, elem_inverse(r));
            }
        }
    };
    feed(prefix, 0); feed(z, 1); feed(suffix, 2);
    for (int j = 0; j < (int)mv.size(); j++) {
        if (mv[j].seg == 0) continue;
        const int want = mv[j].seg - 1;   // the word j may cancel into: P for Z's moves, Z for L's moves
        for (int i = j - 1; i >= 0 && mv[i].seg >= want; i--) {
            if (mv[i].seg == want) {
                // same layers, any power (U2 and U' are both powers of U, though neither is one of the other)
                if (same_cyclic_group(mv[i].e, mv[j].e)) return true;
            }
            // mv[i] stands between j and anything earlier: only a commuting move lets j reach further back
            if (elem_compose(mv[i].e, mv[j].e) != elem_compose(mv[j].e, mv[i].e)) break;
        }
    }
    return false;
}

// The inverse of an algorithm (letters reversed and inverted).
static std::vector<int> inverse_alg(const std::vector<int>& w) {
    std::vector<int> r;
    for (int i = (int)w.size() - 1; i >= 0; i--) r.push_back(inverse_letter(w[i]));
    return r;
}

// Notation: the 27 moves (U, U2, U', Uw, ..., B') and the whole-cube rotations x y z (also x' x2 ...), separated
// by spaces; [x] brackets as printed in solutions are accepted too. Result: path letters (moves, and rotation
// letters as in a solution path). Returns "" or the offending token.
static std::string parse_alg(const std::string& text, std::vector<int>& out) {
    static const std::unordered_map<std::string, int> ROT = {{"x", 1}, {"z", 2}, {"y", 3}};
    out.clear();
    std::string t;
    for (char c : text) t += (c == '[' || c == ']' || c == ',') ? ' ' : c;
    std::istringstream iss(t);
    std::string tok;
    while (iss >> tok) {
        std::string base = tok;
        int turns = 1;
        bool inv = false;
        if (base.size() > 1 && base.back() == '\'') { inv = true; base.pop_back(); }
        else if (base.size() > 1 && base.back() == '2') { turns = 2; base.pop_back(); }
        auto rit = ROT.find(base);
        if (rit != ROT.end()) {
            for (int k = 0; k < turns; k++) out.push_back((inv ? ROTATION_SENTINEL_BASE : P2_OPEN_SENTINEL_BASE) + rit->second);
            continue;
        }
        int found = -1;
        for (int m = 0; m < NUM_MOVES; m++) if (tok == MOVE_NAMES[m]) found = m;
        if (found < 0) return tok;
        out.push_back(found);
    }
    return "";
}

// The forced parts of the current solve (see the header comment).
struct Forced {
    std::vector<int> prefix, suffix;   // P and L as path letters
    int root_rot = -1;                 // rotation the root state was canonicalized by (> 0: Z starts with it)
    long long checked = 0, rejected = 0;   // solved cubes reached / refused because their solution would cancel
    std::vector<int> last_accepted;    // the Z the last accepted check saw (without the root rotation)
    bool active() const { return !prefix.empty() || !suffix.empty(); }
    // The "previous move" the search pretends to have at the start of the normal scramble (side 0: from the end of
    // P) and of the inverse scramble (side 1: from the start of L, since the inverse scramble reads the end of
    // the solution backwards): the usual canonical-order pruning then already keeps Z's first block from merging
    // into P and, with --niss, Z's last block from merging into L. P's trailing block of mutually commuting moves
    // (L's leading block) is reduced to the move of highest rank on its axis: canonical order forbids every move
    // of lower or equal rank after it, which covers all the families in the block. Rotations between the block
    // and the search are pushed through the moves exactly, so a block move is first re-expressed as the move of
    // the 27 turning those layers in the search's frame (a block move that is none of the 27, e.g. Lw, is skipped:
    // no move of the 27 can merge into it). Those rotations are the ones closing P, the root rotation, and
    // `extra`: the rotation letters already sitting in the word the search is at the start of (side 0: N's
    // leading letters in order; side 1: the inverse of I's leading letters, last first -- fork letters go into
    // both words, so they can land at the boundary of Z). NUM_MOVES = nothing to pretend. Only a pruning aid --
    // it forbids a little more than cancelling (lower ranks on the axis), and the exact goal check in search.h
    // stays the authority.
    int virtual_last(int side, const std::vector<int>& extra = std::vector<int>()) const {
        // the block, as moves of the search's frame, nearest to the search first
        std::vector<Elem> block;
        auto commutes_with_all = [&](const Elem& e) {
            for (const Elem& b : block) if (elem_compose(e, b) != elem_compose(b, e)) return false;
            return true;
        };
        if (side == 0) {
            std::vector<int> word = prefix;
            if (root_rot > 0) word.push_back(ROTATION_SENTINEL_BASE + root_rot);
            word.insert(word.end(), extra.begin(), extra.end());
            Elem rot = elem_identity();   // rotations after the move being looked at
            for (int i = (int)word.size() - 1; i >= 0; i--) {
                if (word[i] >= NUM_MOVES) {
                    if (word[i] < NISS_FLIP_TOKEN) rot = elem_compose(rot, rotation_elem(word[i]));   // scanning backwards: earlier rotations act first
                    continue;
                }
                // Z's move z (after rot) merges with this move m iff z is in the group of rot m rot^-1
                Elem e = elem_compose(rot, elem_compose(move_elem(word[i]), elem_inverse(rot)));
                if (!commutes_with_all(e)) break;
                block.push_back(e);
            }
        } else {
            std::vector<int> word = extra;
            word.insert(word.end(), suffix.begin(), suffix.end());
            Elem rot = elem_identity();   // rotations before the move being looked at
            for (size_t i = 0; i < word.size(); i++) {
                if (word[i] >= NUM_MOVES) {
                    if (word[i] < NISS_FLIP_TOKEN) rot = elem_compose(rotation_elem(word[i]), rot);
                    continue;
                }
                // that move, seen from before the rotations, turns rot^-1 m rot
                Elem e = elem_compose(elem_inverse(rot), elem_compose(move_elem(word[i]), rot));
                if (!commutes_with_all(e)) break;
                block.push_back(e);
            }
        }
        int best = NUM_MOVES;
        for (size_t bi = 0; bi < block.size(); bi++) {
            int k = -1;
            for (int c = 0; c < NUM_MOVES && k < 0; c++) if (same_cyclic_group(move_elem(c), block[bi])) k = c;
            if (k < 0) continue;
            if (best == NUM_MOVES || (MOVE_GROUP[k] == MOVE_GROUP[best] && MOVE_RANK[k] > MOVE_RANK[best])) best = k;
        }
        return best;
    }
    int base[2] = {NUM_MOVES, NUM_MOVES};   // virtual_last(side) with no extra letters, set at the start of each solve
};
static Forced g_forced;
