#include <stdint.h>
#include "obliv_sort_config.h"   // uses OBLIV_SORT_N

void sort_N_uint32(uint32_t a[OBLIV_SORT_N]);

typedef struct {
    uint32_t elems[OBLIV_SORT_N];
} InputA;

typedef struct {
    uint32_t elems[OBLIV_SORT_N];
} InputB;

typedef struct {
    uint32_t elems[OBLIV_SORT_N];
} Output;

Output mpc_main(InputA INPUT_A, InputB INPUT_B) {
    uint32_t a[OBLIV_SORT_N];
    for (int i = 0; i < OBLIV_SORT_N; i++) {
        a[i] = INPUT_A.elems[i] ^ INPUT_B.elems[i];
    }
    sort_N_uint32(a);

    Output out;
    for (int i = 0; i < OBLIV_SORT_N; i++) {
        out.elems[i] = a[i];
    }
    return out;
}
