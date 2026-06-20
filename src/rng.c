#include "rng.h"
#include <math.h>

static inline uint64_t rotl(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

/* splitmix64 to expand a single seed into the 256-bit state. */
static uint64_t splitmix64(uint64_t *x)
{
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void rng_seed(Rng *r, uint64_t seed)
{
    uint64_t sm = seed ? seed : 0xDEADBEEFCAFEF00DULL;
    for (int i = 0; i < 4; i++) r->s[i] = splitmix64(&sm);
}

uint64_t rng_next(Rng *r)
{
    uint64_t *s = r->s;
    const uint64_t result = rotl(s[1] * 5, 7) * 9;
    const uint64_t t = s[1] << 17;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3]  = rotl(s[3], 45);

    return result;
}

double rng_double(Rng *r)
{
    /* top 53 bits -> uniform [0,1) */
    return (double)(rng_next(r) >> 11) * (1.0 / 9007199254740992.0);
}

double rng_exp(Rng *r, double mean)
{
    double u = rng_double(r);
    if (u <= 1e-12) u = 1e-12;
    return -mean * log(u);
}

int rng_range(Rng *r, int lo, int hi)
{
    if (hi <= lo) return lo;
    uint64_t span = (uint64_t)(hi - lo + 1);
    return lo + (int)(rng_next(r) % span);
}
