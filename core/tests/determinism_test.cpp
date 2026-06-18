// T1 tests for the physics core:
//   1. determinism      -- same seed + same call pattern -> identical event stream
//   2. block-size inv.  -- same total sim time in 1x-dt vs 50x-dt chunks -> identical
//   3. rng determinism  -- Pcg32: same seed same stream, different seed different stream
//                          (tested directly, since the nudge RNG only matters when the
//                           ball is stuck -- a clean cascade correctly ignores the seed)
//   4. liveliness       -- the ball actually cascades, hits pegs, and loops (not inert)
#include "PhysicsCore.h"
#include "Rng.h"
#include <cstdio>
#include <cstring>
#include <vector>

static constexpr double DT = 1.0 / 1000.0;

struct RunResult {
    std::vector<Collision> ev;
    int    loops = 0;
    double ballY = 0.0;
    int    pegs = 0;
};

static RunResult run(uint64_t seed, double chunk, int nCalls) {
    BoardParams p;
    makeStaggeredBoard(p);
    PhysicsWorld w;
    w.init(seed, p);
    RunResult r;
    r.pegs = w.dbgPegCount();
    for (int i = 0; i < nCalls; ++i) w.advance(chunk, r.ev);
    r.loops = w.loopIndex();
    r.ballY = w.dbgBallY();
    w.shutdown();
    return r;
}

static bool bitSame(const std::vector<Collision>& a, const std::vector<Collision>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].loop != b[i].loop) return false;
        if (std::memcmp(&a[i].t,      &b[i].t,      sizeof(double)) != 0) return false;
        if (std::memcmp(&a[i].nx,     &b[i].nx,     sizeof(float))  != 0) return false;
        if (std::memcmp(&a[i].ny,     &b[i].ny,     sizeof(float))  != 0) return false;
        if (std::memcmp(&a[i].energy, &b[i].energy, sizeof(float))  != 0) return false;
    }
    return true;
}

int main() {
    auto A = run(12345, DT,        10000);  // 10s baseline
    auto B = run(12345, DT,        10000);  // identical inputs -> determinism
    auto C = run(12345, 50.0 * DT, 200);    // same 10000 steps, big chunks -> block-size inv.

    // RNG checked directly (decoupled from whether the sim triggers a nudge).
    Pcg32 r1, r2, r3; r1.seed(7); r2.seed(7); r3.seed(8);
    bool rngDet = true, rngSens = false;
    for (int i = 0; i < 64; ++i) {
        uint32_t a = r1.next(), b = r2.next(), c = r3.next();
        if (a != b) rngDet = false;
        if (a != c) rngSens = true;
    }

    bool det  = bitSame(A.ev, B.ev);
    bool blk  = bitSame(A.ev, C.ev);
    bool live = (A.ev.size() >= 10) && (A.loops >= 1);

    std::printf("[board] pegs=%d  events(10s)=%zu  loops=%d  finalBallY=%.3f\n",
                A.pegs, A.ev.size(), A.loops, A.ballY);
    std::printf("determinism (same seed, same calls):     %s\n", det     ? "PASS" : "FAIL");
    std::printf("block-size invariance (1x vs 50x dt):    %s\n", blk     ? "PASS" : "FAIL");
    std::printf("rng determinism (same seed = same):      %s\n", rngDet  ? "PASS" : "FAIL");
    std::printf("rng sensitivity (diff seed = diff):      %s\n", rngSens ? "PASS" : "FAIL");
    std::printf("liveliness (cascades + loops, not inert):%s\n", live    ? "PASS" : "FAIL");

    bool ok = det && blk && rngDet && rngSens && live;
    std::printf("RESULT: %s\n", ok ? "ALL PASS" : "FAILED");
    return ok ? 0 : 1;
}
