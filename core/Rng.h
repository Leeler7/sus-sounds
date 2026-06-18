// Rng.h -- portable, deterministic PRNG (PCG32).
//
// Determinism rule (ARCHITECTURE.md §5.1): NEVER use std::*_distribution -- their
// algorithms are implementation-defined and differ across libstdc++/libc++/MSVC,
// which silently breaks cross-platform determinism. This is self-contained integer
// math with our own int->float mapping, so it is bit-identical on every platform.
#pragma once
#include <cstdint>

struct Pcg32 {
    uint64_t state = 0;
    uint64_t inc   = 1;

    // Seed deterministically. (seed, seq) fully determines the stream.
    void seed(uint64_t s, uint64_t seq = 1) {
        state = 0u;
        inc = (seq << 1u) | 1u;
        next();
        state += s;
        next();
    }

    uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }

    // [0,1) with 24 bits of mantissa -- platform-stable.
    float nextFloat() { return (next() >> 8) * (1.0f / 16777216.0f); }

    // [-1,1)
    float nextSigned() { return nextFloat() * 2.0f - 1.0f; }
};
