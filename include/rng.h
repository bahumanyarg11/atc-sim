#ifndef ATC_RNG_H
#define ATC_RNG_H

#include <stdint.h>

/*
 * xoshiro256** — a small, fast, high-quality PRNG.
 * Self-contained so simulation runs are reproducible from a seed,
 * independent of the host libc's rand() implementation.
 */

typedef struct { uint64_t s[4]; } Rng;

void   rng_seed(Rng *r, uint64_t seed);
uint64_t rng_next(Rng *r);
double rng_double(Rng *r);              /* [0,1)                       */
double rng_exp(Rng *r, double mean);    /* exponential interarrival    */
int    rng_range(Rng *r, int lo, int hi); /* inclusive [lo,hi]         */

#endif /* ATC_RNG_H */
