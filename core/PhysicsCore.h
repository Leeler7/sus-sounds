// PhysicsCore.h -- deterministic, sample-rate-independent Plinko physics (T1).
//
// JUCE-free on purpose: this is pure logic + Box2D v3, so it unit-tests in isolation
// and drops into the plugin later. Determinism invariants (ARCHITECTURE.md §2, §2.1):
//   - Box2D v3.1.1, no -ffast-math/FMA (set in CMake).
//   - Fixed sim timestep (SIM_DT), advanced by an accumulator keyed to ELAPSED TIME,
//     not audio sample count -> identical physics at 44.1/48/96k AND independent of
//     the caller's block size.
//   - Portable PCG32 RNG (Rng.h) for the no-stuck nudge only.
//
//   TIME MODEL
//   caller: advance(seconds)          // however the audio thread chunks time
//     accum += seconds
//     while (accum >= SIM_DT):        // fixed steps -> block-size invariant
//        stepOnce(); accum -= SIM_DT
//
//   BALL LIFECYCLE
//   [drop] -> FALLING --hit peg--> emit Collision --\
//      ^                                            |
//      |  low energy -> seeded nudge                |
//      \-- exit (y<=exitY) OR timeout -> reset() ---/
#pragma once
#include <cstdint>
#include <vector>
#include "Rng.h"
#include <box2d/box2d.h>

struct BoardParams {
    float width   = 1.0f;     // board spans x in [0, width]
    float topY    = 1.4f;     // y in [0, topY]; drop near top, exit near bottom
    float gravity = 22.0f;    // magnitude; applied as (0, -gravity). (Higher = faster fall but FEWER peg contacts.)
    float ballRadius = 0.03f;   // gaps must exceed the ball diameter for a clean cascade
    float pegRadius  = 0.022f;  // smaller than the ball (reads as distinct dots; user builds the board)
    float restitution = 0.5f; // middle ground. NOTE: cascade richness still needs by-ear
                              // tuning once audio is wired (the deterministic core is correct;
                              // a near-centered landing can still balance on a peg until the
                              // no-stuck nudge frees it -- see ENG-LAYOUT T1 follow-up).
    float dropX = 0.42f;      // OFF the peg columns/gaps: a perfectly symmetric drop would
                              // balance on the apex peg forever (no asymmetry to deflect it)
    float dropY = 1.33f;
    float initialVx = 0.6f;   // small fixed sideways velocity at spawn -> breaks symmetry,
                              // guarantees a cascade (deterministic: same loop every time)
    float exitY = 0.04f;      // ball below this = exited -> respawn
    float energyFloor = 0.05f;// below this speed = "slow"
    float nudge = 0.8f;       // desired horizontal velocity kick when genuinely stuck (m/s)
    int   stuckSteps = 80;    // must be slow this many sim steps (~0.08s) before nudging
    double maxLoopSeconds = 8.0; // hard timeout backstop -> teleport+respawn

    int   pegCount = 0;
    float pegX[128];
    float pegY[128];
    float pegRest[128];       // per-peg restitution. Defaults to `restitution`; > 1.0 makes a
                              // "bumper" that returns EXTRA energy (like a pinball bumper).
                              // Keep <= ~1.6 so the ball can't gain runaway energy.
    int   pegType[128];       // per-peg routing: 0 = delay peg (rhythmic echo), 1 = reverb peg (splash)
};

struct Collision {
    double t;      // seconds since init (monotonic sim time)
    float  nx, ny; // normalized board position [0,1]
    float  energy; // ball speed at contact
    int    loop;   // which loop iteration produced it
    int    type;   // peg type hit: 0 = delay, 1 = reverb
};

// Fill p with a deterministic staggered Plinko grid (no RNG -> layout is data, not chance).
void makeStaggeredBoard(BoardParams& p, int rows = 13, int cols = 5);

class PhysicsWorld {
public:
    void init(uint64_t seed, const BoardParams& params);
    void shutdown();
    ~PhysicsWorld();

    // Advance the simulation by `seconds`, appending any collisions to `out`.
    void advance(double seconds, std::vector<Collision>& out);

    int    loopIndex() const { return loop_; }
    double simTime()   const { return simTime_; }

    // debug probes (T1 bring-up)
    float     dbgBallY();
    float     dbgBallX();
    int       dbgPegCount() const { return p_.pegCount; }
    long long dbgRawBegins() const { return rawBegins_; }

    // live control (GUI) -- these modify the running world WITHOUT re-init (ball preserved)
    void setGravity(float g);
    bool addPeg(float x, float y, float rest, int type);  // false if full
    void movePeg(int i, float x, float y);
    void removePeg(int i);
    void setPegType(int i, int type);
    int  pegCount() const { return p_.pegCount; }
    const BoardParams& boardParams() const { return p_; }

private:
    static constexpr double SIM_DT = 1.0 / 1000.0; // fixed 1 kHz sim step
    static constexpr int    SUBSTEPS = 4;

    void stepOnce(std::vector<Collision>& out);
    void respawn();
    void createPegBody(int i);   // build the Box2D body+shape for peg i (type encoded in userData)

    BoardParams p_{};
    uint64_t baseSeed_ = 0;
    Pcg32 rng_{};
    int loop_ = 0;
    double elapsed_ = 0.0;   // total time fed in (accumulated)
    long long steps_ = 0;    // sim steps taken so far (target derived by round-to-nearest)
    double simTime_ = 0.0;
    double loopStart_ = 0.0;
    long long rawBegins_ = 0;   // debug: total raw begin-touch events seen
    int slowCount_ = 0;         // consecutive sim steps the ball has been "slow"
    b2BodyId  pegBody_[128]{};  // per-peg Box2D body (for live move/delete)
    b2ShapeId pegShape_[128]{}; // per-peg shape (for live type change via userData)

    b2WorldId world_{};
    b2BodyId  ball_{};
    bool      inited_ = false;
};
