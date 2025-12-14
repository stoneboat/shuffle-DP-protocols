// tree_mech.c
//
// Oblivious Fenwick (BIT) tree mechanism for continual release of noisy prefix sums.
//
// Batch interface:
//   Inputs:
//     - x_flat[t,k]      for t=0..NUM_STEPS-1, k=0..VEC_DIM-1  (flattened row-major)
//     - noise_flat[i,k]  for i=1..NUM_STEPS (Fenwick node index), k=0..VEC_DIM-1
//                        stored as noise_flat[(i-1)*VEC_DIM + k] (so the array has NUM_STEPS*VEC_DIM entries)
//   Output:
//     - ps_flat[t,k] for t=0..NUM_STEPS-1, k=0..VEC_DIM-1, where
//         ps[t] = sum_{τ=0..t} x[τ] + sum_{i in FenwickCover(t+1)} noise[i].
//
// Obliviousness notes:
//   - No heap allocation.
//   - All loops have compile-time bounds: NUM_STEPS, VEC_DIM, LOG_STEPS.
//   - No secret-dependent branches; masking is used for conditional effects.
//   - Array indices are forced in-range even when a step is inactive, to avoid OOB.
//
// Configuration is provided by tree_mech_config.h, expected to define at least:
//   NUM_STEPS, VEC_DIM, LOG_STEPS
// Optionally: NPARTS, FRAC_BITS (not required here unless you scale elsewhere).

#include <stdint.h>
#include <stddef.h>

#include "tree_mech_config.h"

// -------------------- Constant-time helpers (branch-free on secrets) --------------------

static inline uint64_t ct_mask_u64(uint32_t bit01) {
    // 0xFFFFFFFFFFFFFFFF if bit01==1 else 0
    return 0ull - (uint64_t)bit01;
}

static inline uint32_t ct_mask_u32(uint32_t bit01) {
    // 0xFFFFFFFF if bit01==1 else 0
    return 0u - bit01;
}

static inline uint32_t ct_select_u32(uint32_t bit01, uint32_t a, uint32_t b) {
    uint32_t m = ct_mask_u32(bit01);
    return (a & m) | (b & ~m);
}

static inline int64_t ct_select_i64(uint32_t bit01, int64_t a, int64_t b) {
    uint64_t m  = ct_mask_u64(bit01);
    uint64_t ua = (uint64_t)a;
    uint64_t ub = (uint64_t)b;
    uint64_t ur = (ua & m) | (ub & ~m);
    return (int64_t)ur;
}

// Least-significant-bit for unsigned i. For i==0, returns 0.
static inline uint32_t lsb_u32(uint32_t i) {
    // Two's-complement trick: i & -i
    return i & (0u - i);
}

// Safe index mapping for in-range access:
// If active==1, idx = i (assumed in [1..NUM_STEPS]).
// If active==0, idx = 1 (a fixed, valid node).
static inline uint32_t safe_node_index(uint32_t active01, uint32_t i) {
    // i is uint32_t; if inactive, force to 1 to avoid OOB.
    return ct_select_u32(active01, i, 1u);
}

// Flattened indexing helpers
static inline size_t sum_off(uint32_t node1, uint32_t k) {
    // node1 is 1-indexed in [0..NUM_STEPS], but we only store 0..NUM_STEPS
    return (size_t)node1 * (size_t)VEC_DIM + (size_t)k;
}
static inline size_t x_off(uint32_t t0, uint32_t k) {
    return (size_t)t0 * (size_t)VEC_DIM + (size_t)k;
}
static inline size_t noise_off(uint32_t node1, uint32_t k) {
    // node1 is 1-indexed in [1..NUM_STEPS]; stored at (node1-1)
    return (size_t)(node1 - 1u) * (size_t)VEC_DIM + (size_t)k;
}

// -------------------- Main API --------------------
//
// x_flat      : length NUM_STEPS*VEC_DIM
// noise_flat  : length NUM_STEPS*VEC_DIM (node 1 at offset 0)
// ps_flat     : length NUM_STEPS*VEC_DIM
//
void tree_mechanism_prefix_sums(
    const int64_t *x_flat,
    const int64_t *noise_flat,
    int64_t *ps_flat
) {
    // Fenwick sums: sum[node][k], node in [0..NUM_STEPS], sum[0] unused but allocated.
    int64_t sum[(NUM_STEPS + 1) * VEC_DIM];

    // 0) Initialize sums to 0 (fixed loops)
    for (uint32_t node = 0; node <= (uint32_t)NUM_STEPS; node++) {
        for (uint32_t k = 0; k < (uint32_t)VEC_DIM; k++) {
            sum[sum_off(node, k)] = 0;
        }
    }

    // 1) Build Fenwick sums by streaming updates x[t]
    //
    // For each t (0-indexed), BIT index i=t+1 (1..NUM_STEPS).
    // Repeatedly:
    //   sum[i] += x[t]
    //   i += lsb(i)
    //
    // We run exactly LOG_STEPS iterations; when i > NUM_STEPS, we mask out updates.
    for (uint32_t t = 0; t < (uint32_t)NUM_STEPS; t++) {
        uint32_t i = t + 1u;

        for (uint32_t step = 0; step < (uint32_t)LOG_STEPS; step++) {
            // active is public: depends only on i (which depends on public t/step)
            uint32_t active = (uint32_t)(i <= (uint32_t)NUM_STEPS);

            // force index in-range for memory access
            uint32_t idx = safe_node_index(active, i);

            // sum[idx] += active ? x[t] : 0
            for (uint32_t k = 0; k < (uint32_t)VEC_DIM; k++) {
                int64_t delta = x_flat[x_off(t, k)];
                int64_t add   = ct_select_i64(active, delta, 0);
                sum[sum_off(idx, k)] += add;
            }

            // advance i := active ? (i + lsb(i)) : i
            // lsb(i) is well-defined for i>=1; for i>NUM_STEPS it's still fine but masked.
            uint32_t inc = lsb_u32(i);
            uint32_t nxt = i + inc;
            i = ct_select_u32(active, nxt, i);
        }
    }

    // 2) Answer all noisy prefix sums ps[t]
    //
    // Query for time t uses BIT index i=t+1 and repeats:
    //   acc += sum[i] + noise[i]
    //   i -= lsb(i)
    //
    // We run exactly LOG_STEPS iterations; when i==0, we mask out additions.
    for (uint32_t t = 0; t < (uint32_t)NUM_STEPS; t++) {
        int64_t acc[VEC_DIM];
        for (uint32_t k = 0; k < (uint32_t)VEC_DIM; k++) {
            acc[k] = 0;
        }

        uint32_t i = t + 1u;

        for (uint32_t step = 0; step < (uint32_t)LOG_STEPS; step++) {
            uint32_t active = (uint32_t)(i > 0u);

            // force node index in-range for reads (must be in [1..NUM_STEPS])
            uint32_t idx = safe_node_index(active, i);

            for (uint32_t k = 0; k < (uint32_t)VEC_DIM; k++) {
                int64_t s = sum[sum_off(idx, k)];
                int64_t n = noise_flat[noise_off(idx, k)];
                int64_t term = s + n;
                acc[k] += ct_select_i64(active, term, 0);
            }

            uint32_t dec = lsb_u32(i);          // lsb(0)=0, safe
            uint32_t nxt = i - dec;             // when i>0, dec>0
            i = ct_select_u32(active, nxt, i);  // if inactive (i==0), keep i==0
        }

        for (uint32_t k = 0; k < (uint32_t)VEC_DIM; k++) {
            ps_flat[x_off(t, k)] = acc[k];
        }
    }
}

/*
 * Configuration guidance:
 *   LOG_STEPS must be large enough so that, for any start i in [1..NUM_STEPS],
 *   after LOG_STEPS Fenwick hops, Update has i > NUM_STEPS and Query has i == 0.
 *
 * A safe choice is:
 *   LOG_STEPS >= ceil(log2(NUM_STEPS)) + 1.
 *
 * Examples:
 *   NUM_STEPS=8    -> LOG_STEPS >= 4
 *   NUM_STEPS=64   -> LOG_STEPS >= 7
 *   NUM_STEPS=1024 -> LOG_STEPS >= 11
 */
