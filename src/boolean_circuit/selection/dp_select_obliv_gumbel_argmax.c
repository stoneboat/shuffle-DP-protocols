// dp_select_obliv_gumbel_argmax.c
//
// Oblivious DP selection via report-noisy-max (Gumbel-max), assuming
// pre-generated Gumbel noise already scaled appropriately.
//
// Fixed-size (macro-parameterized) implementation in the same style as
// oblivious sorting code: no heap allocation, fixed loop bounds.
//
// Data model here:
//   - NUM_RECORDS = n
//   - NUM_CHOICES = d
//   - x_bits is a flat (n*d) array, row-major: x_bits[i*NUM_CHOICES + j] in {0,1}
//   - gumbel_scaled[j] is fixed-point noise with FRAC_BITS fractional bits,
//     already scaled for your DP parameterization.
//
// Note: NPARTS is included as a macro as requested (not used in this file).

#include <stdint.h>
#include <limits.h>

#include "obliv_gumbel_config.h"

// -------------------- Constant-time helpers (branch-free on secrets) --------------------

static inline uint32_t ct_mask_u32(uint32_t bit01) {
    // 0xFFFFFFFF if bit01==1 else 0
    return 0u - bit01;
}

static inline uint64_t ct_mask_u64(uint32_t bit01) {
    // 0xFFFFFFFFFFFFFFFF if bit01==1 else 0
    return 0ull - (uint64_t)bit01;
}

static inline uint32_t ct_select_u32(uint32_t bit01, uint32_t a, uint32_t b) {
    // if bit01==1 return a else b
    uint32_t m = ct_mask_u32(bit01);
    return (a & m) | (b & ~m);
}

static inline int64_t ct_select_i64(uint32_t bit01, int64_t a, int64_t b) {
    // if bit01==1 return a else b
    uint64_t m  = ct_mask_u64(bit01);
    uint64_t ua = (uint64_t)a;
    uint64_t ub = (uint64_t)b;
    uint64_t ur = (ua & m) | (ub & ~m);
    return (int64_t)ur;
}

static inline uint32_t ct_is_zero_u64(uint64_t x) {
    // 1 if x==0 else 0, branch-free.
    return (uint32_t)((((x | (uint64_t)(0ull - x)) >> 63) ^ 1ull) & 1ull);
}

static inline uint32_t ct_eq_i64(int64_t a, int64_t b) {
    // 1 if a==b else 0
    return ct_is_zero_u64((uint64_t)(a ^ b));
}

static inline uint32_t ct_lt_u32(uint32_t a, uint32_t b) {
    // 1 if a<b else 0 (typically compiles to setcc)
    return (uint32_t)(a < b);
}

static inline uint32_t ct_gt_i64(int64_t a, int64_t b) {
    // Signed compare without branches:
    // map signed to ordered unsigned by flipping sign bit.
    uint64_t ua = ((uint64_t)a) ^ 0x8000000000000000ull;
    uint64_t ub = ((uint64_t)b) ^ 0x8000000000000000ull;
    return (uint32_t)(ua > ub);
}

// -------------------- Main API --------------------
//
// Returns selected index j* in [0, NUM_CHOICES-1].
//
uint32_t dp_select_obliv_gumbel_argmax(
    const uint8_t *x_bits,          // length NUM_RECORDS * NUM_CHOICES, bytes in {0,1}
    const int64_t *gumbel_scaled    // length NUM_CHOICES, fixed-point, already scaled
) {
    // Accumulator: count per choice (no heap).
    uint32_t count[NUM_CHOICES];

    // 0) Initialize counts (fixed loop).
    for (uint32_t j = 0; j < (uint32_t)NUM_CHOICES; j++) {
        count[j] = 0u;
    }

    // 1) Oblivious aggregation (fixed loops, fixed access pattern).
    // count[j] = sum_i x[i,j]
    for (uint32_t i = 0; i < (uint32_t)NUM_RECORDS; i++) {
        const uint8_t *row = x_bits + (uint32_t)i * (uint32_t)NUM_CHOICES;
        for (uint32_t j = 0; j < (uint32_t)NUM_CHOICES; j++) {
            // Treat input as a bit in {0,1}. Mask with 1 to be robust to stray values.
            uint32_t x = (uint32_t)(row[j] & 1u);
            count[j] += x;
        }
    }

    // 2) Tournament argmax over noisy scores:
    // score[j] = (count[j] << FRAC_BITS) + gumbel_scaled[j]
    // Tie-break: smaller index wins if scores equal.
    uint32_t best_idx = 0u;
    int64_t best_score = (((int64_t)count[0]) << FRAC_BITS) + gumbel_scaled[0];

    for (uint32_t j = 1u; j < (uint32_t)NUM_CHOICES; j++) {
        int64_t cand_score = (((int64_t)count[j]) << FRAC_BITS) + gumbel_scaled[j];

        uint32_t gt = ct_gt_i64(cand_score, best_score);
        uint32_t eq = ct_eq_i64(cand_score, best_score);
        uint32_t lt = ct_lt_u32(j, best_idx);
        uint32_t swap = gt | (eq & lt);

        best_score = ct_select_i64(swap, cand_score, best_score);
        best_idx   = ct_select_u32(swap, j,          best_idx);
    }

    return best_idx;    
}
