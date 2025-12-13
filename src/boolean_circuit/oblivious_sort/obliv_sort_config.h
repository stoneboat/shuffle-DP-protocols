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

#define OBLIV_SORT_W 32  // bit-width per element (uint32_t)
