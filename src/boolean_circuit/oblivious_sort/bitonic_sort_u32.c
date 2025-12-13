// src/boolean_circuit/oblivious_sort/bitonic_sort_u32.c
#include <stdint.h>

#include "obliv_sort_config.h"

// Branch-free conditional swap using a 0/1 flag "swap".
// If swap=1, swaps *x and *y; else leaves them unchanged.
static inline void cswap_u32(uint32_t *x, uint32_t *y, uint32_t swap) {
    // swap must be 0 or 1
    uint32_t mask = (uint32_t)(0u - swap);     // 0xFFFFFFFF if swap=1 else 0
    uint32_t t = (*x ^ *y) & mask;
    *x ^= t;
    *y ^= t;
}

// Ascending compare-swap: ensures *x <= *y.
static inline void compare_swap_asc_u32(uint32_t *x, uint32_t *y) {
    // swap = 1 iff *x > *y
    uint32_t swap = (uint32_t)(*x > *y);
    cswap_u32(x, y, swap);
}

// Descending compare-swap: ensures *x >= *y.
static inline void compare_swap_desc_u32(uint32_t *x, uint32_t *y) {
    // swap = 1 iff *x < *y
    uint32_t swap = (uint32_t)(*x < *y);
    cswap_u32(x, y, swap);
}

// Bitonic sorting network (Batcher) using the XOR-pairing schedule.
// Requires OBLIV_SORT_N to be a power of two (OBLIV_SORT_N=16 is fine).
void sort_N_uint32(uint32_t a[OBLIV_SORT_N]) {
    // k = size of subsequences being merged into bitonic sequences
    for (uint32_t k = 2; k <= (uint32_t)OBLIV_SORT_N; k <<= 1) {
        // j = distance of comparators in the current stage
        for (uint32_t j = k >> 1; j > 0; j >>= 1) {
            for (uint32_t i = 0; i < (uint32_t)OBLIV_SORT_N; i++) {
                uint32_t l = i ^ j;
                // Handle each pair once.
                // This condition depends only on indices (public), so it is fine.
                if (l > i) {
                    // Direction depends only on (i & k), also public.
                    // If (i & k) == 0, we sort ascending; else descending.
                    if ((i & k) == 0) {
                        compare_swap_asc_u32(&a[i], &a[l]);
                    } else {
                        compare_swap_desc_u32(&a[i], &a[l]);
                    }
                }
            }
        }
    }
}
