// src/boolean_circuit/oblivious_sort/main_test_bitonic_sort_u32.c
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef N
#define N 16
#endif

// Declaration from bitonic_sort_u32.c
void sort_N_uint32(uint32_t a[N]);

static int cmp_u32_qsort(const void *pa, const void *pb) {
    uint32_t a = *(const uint32_t *)pa;
    uint32_t b = *(const uint32_t *)pb;
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

static int is_sorted_non_decreasing(const uint32_t a[N]) {
    for (int i = 1; i < N; i++) {
        if (a[i-1] > a[i]) return 0;
    }
    return 1;
}

static void dump_arr(const char *tag, const uint32_t a[N]) {
    printf("%s:", tag);
    for (int i = 0; i < N; i++) {
        printf(" %u", a[i]);
    }
    printf("\n");
}

static void one_test(const uint32_t in[N], int *ok) {
    uint32_t got[N];
    uint32_t ref[N];
    memcpy(got, in, sizeof(got));
    memcpy(ref, in, sizeof(ref));

    sort_N_uint32(got);
    qsort(ref, N, sizeof(uint32_t), cmp_u32_qsort);

    if (memcmp(got, ref, sizeof(got)) != 0) {
        *ok = 0;
        printf("Mismatch!\n");
        dump_arr("in ", in);
        dump_arr("got", got);
        dump_arr("ref", ref);
    } else if (!is_sorted_non_decreasing(got)) {
        *ok = 0;
        printf("Not sorted!\n");
        dump_arr("in ", in);
        dump_arr("got", got);
    }
}

int main(void) {
    int ok = 1;

    // Deterministic seed for reproducibility; change if desired.
    srand(1);

    // 1) Adversarial cases
    {
        uint32_t a1[N];
        for (int i = 0; i < N; i++) a1[i] = 0;                 // all equal
        one_test(a1, &ok);

        uint32_t a2[N];
        for (int i = 0; i < N; i++) a2[i] = (uint32_t)i;       // already sorted
        one_test(a2, &ok);

        uint32_t a3[N];
        for (int i = 0; i < N; i++) a3[i] = (uint32_t)(N-1-i); // reverse sorted
        one_test(a3, &ok);

        uint32_t a4[N];
        for (int i = 0; i < N; i++) a4[i] = (uint32_t)(i % 4); // many duplicates
        one_test(a4, &ok);

        uint32_t a5[N] = {
            0u, 4294967295u, 1u, 4294967294u,
            2u, 3u, 4u, 5u,
            100u, 99u, 98u, 97u,
            50u, 60u, 70u, 80u
        };
        one_test(a5, &ok);
    }

    // 2) Random tests
    for (int t = 0; t < 200; t++) {
        uint32_t a[N];
        for (int i = 0; i < N; i++) {
            // combine rand() calls to get more bits than RAND_MAX
            uint32_t r1 = (uint32_t)rand();
            uint32_t r2 = (uint32_t)rand();
            a[i] = (r1 << 16) ^ (r2 & 0xFFFFu);
        }
        one_test(a, &ok);
        if (!ok) break;
    }

    if (ok) {
        printf("PASS (N=%d)\n", N);
        return 0;
    } else {
        printf("FAIL (N=%d)\n", N);
        return 1;
    }
}
