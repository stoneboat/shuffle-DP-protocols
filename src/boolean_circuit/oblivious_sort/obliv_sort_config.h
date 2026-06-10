#pragma once
#include <stdint.h>

/*
 * Single source of truth for oblivious sort parameters.
 *
 * You can override at compile time via:
 *   gcc ... -DOBLIV_SORT_N=16
 */
#ifndef OBLIV_SORT_N
#define OBLIV_SORT_N 16
#endif

// Bit-width per element. Allow override at compile time to align with
// the circuit generation settings.
#ifndef OBLIV_SORT_W
#define OBLIV_SORT_W 32
#endif
