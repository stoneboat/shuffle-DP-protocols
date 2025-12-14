#include <stdint.h>
#include <stddef.h>

#include "obliv_gumbel_config.h"

enum {
    X_BITS = NUM_RECORDS * NUM_CHOICES
};

uint32_t dp_select_obliv_gumbel_argmax(
    const uint8_t *x_bits,
    const int64_t *gumbel_scaled);

typedef struct {
    uint8_t x_bits[X_BITS];
    int64_t gumbel_scaled[NUM_CHOICES];
} InputA;

typedef struct {
    uint8_t x_bits[X_BITS];
    int64_t gumbel_scaled[NUM_CHOICES];
} InputB;

typedef struct {
    uint32_t selected_index;
} Output;

Output mpc_main(InputA INPUT_A, InputB INPUT_B) {
    uint8_t x_bits[X_BITS];
    int64_t gumbel_scaled[NUM_CHOICES];

    for (int idx = 0; idx < X_BITS; idx++) {
        x_bits[idx] = (INPUT_A.x_bits[idx] ^ INPUT_B.x_bits[idx]) & 1u;
    }

    for (int j = 0; j < NUM_CHOICES; j++) {
        gumbel_scaled[j] = INPUT_A.gumbel_scaled[j] ^ INPUT_B.gumbel_scaled[j];
    }

    uint32_t argmax = dp_select_obliv_gumbel_argmax(x_bits, gumbel_scaled);

    Output out;
    out.selected_index = argmax;
    return out;
}
