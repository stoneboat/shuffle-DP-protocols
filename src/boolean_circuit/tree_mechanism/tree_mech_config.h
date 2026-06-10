#ifndef NPARTS
#define NPARTS 8
#endif

#ifndef NUM_STEPS          // T
#define NUM_STEPS 8
#endif

#ifndef VEC_DIM            // D
#define VEC_DIM 8
#endif

#ifndef FRAC_BITS
#define FRAC_BITS 32
#endif

// Fixed upper bound on number of BIT hops (≈ ceil(log2(NUM_STEPS))+1).
// Define this as a macro to avoid dynamic loop bounds in CBMC-GC.
#ifndef LOG_STEPS
#define LOG_STEPS 4        // example: enough for NUM_STEPS <= 16
#endif