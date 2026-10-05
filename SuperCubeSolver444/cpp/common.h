// Shared includes for the S0 -> solved supercube solver. Every other header
// includes this (directly or transitively); the solver is a single translation
// unit (main.cpp includes everything), so all helpers are `static`.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "tables.h"           // 27 moves, center/wing perms, LR/FB reuse mapping (generate_tables.py)
#include "tables2.h"          // P2 (21-move) index space, FB perms (generate_tables2.py)
#include "tables4.h"          // POS_SLOTS/NEG_OF/WING_ADJ: wing-pairing orbit layout
#include "tables5.h"          // CORNER_PERM: corner-sticker permutation
#include "rotations.h"        // whole-cube rotation classes
#include "pairing_dist_io.h"  // 12-perm rank/unrank, dense index, nibble-packed tables

// --startup-times: how long each table-building step (and the laps inside the big ones) takes.
static bool g_startup_times = false;
struct LapTimer {
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
    void lap(const char* what) {
        const auto n = std::chrono::steady_clock::now();
        if (g_startup_times) printf("        [startup]   %-56s %8.1f ms\n", what, std::chrono::duration<double, std::milli>(n - t).count());
        t = n;
    }
};
