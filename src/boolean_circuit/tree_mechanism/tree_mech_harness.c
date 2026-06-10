// tree_mech_harness.c
//
// Harness for CBMC-GC style 2-party XOR sharing.
// Reconstructs secret inputs x_flat and noise_flat from INPUT_A ^ INPUT_B,
// runs tree_mechanism_prefix_sums, and returns the full ps_flat as output.
//
// Layout conventions (must match tree_mech.c):
//   - x_flat has length NUM_STEPS * VEC_DIM, row-major: x_flat[t*VEC_DIM + k]
//   - noise_flat has length NUM_STEPS * VEC_DIM, node-major (1-indexed nodes stored at node-1):
//       noise_flat[(node-1)*VEC_DIM + k], for node=1..NUM_STEPS
//   - ps_flat output has length NUM_STEPS * VEC_DIM, same as x_flat.
//

#include <stdint.h>
#include <stddef.h>

#include "tree_mech_config.h"

enum {
    X_LEN     = NUM_STEPS * VEC_DIM,
    NOISE_LEN = NUM_STEPS * VEC_DIM,
    OUT_LEN   = NUM_STEPS * VEC_DIM
};

// Function under test (from tree_mech.c)
void tree_mechanism_prefix_sums(
    const int64_t *x_flat,
    const int64_t *noise_flat,
    int64_t *ps_flat
);

typedef struct {
    int64_t x_flat[X_LEN];
    int64_t noise_flat[NOISE_LEN];
} InputA;

typedef struct {
    int64_t x_flat[X_LEN];
    int64_t noise_flat[NOISE_LEN];
} InputB;

// IMPORTANT: use an array field (like the sort harness) to avoid output detection issues.
typedef struct {
    int64_t elems[OUT_LEN];
} Output;

Output mpc_main(InputA INPUT_A, InputB INPUT_B) {
    int64_t x_flat[X_LEN];
    int64_t noise_flat[NOISE_LEN];
    int64_t ps_flat[OUT_LEN];

    // Reconstruct secrets by XORing the two shares bitwise.
    // Use uint64_t XOR to preserve two's-complement bit patterns.
    for (size_t i = 0; i < (size_t)X_LEN; i++) {
        uint64_t a = (uint64_t)INPUT_A.x_flat[i];
        uint64_t b = (uint64_t)INPUT_B.x_flat[i];
        x_flat[i] = (int64_t)(a ^ b);
    }

    for (size_t i = 0; i < (size_t)NOISE_LEN; i++) {
        uint64_t a = (uint64_t)INPUT_A.noise_flat[i];
        uint64_t b = (uint64_t)INPUT_B.noise_flat[i];
        noise_flat[i] = (int64_t)(a ^ b);
    }

    // Run the mechanism
    tree_mechanism_prefix_sums(x_flat, noise_flat, ps_flat);

    // Return output
    Output out;
    for (size_t i = 0; i < (size_t)OUT_LEN; i++) {
        out.elems[i] = ps_flat[i];
    }
    return out;
}
