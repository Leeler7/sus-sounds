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
    float ballRest   = 0.5f;    // ball springiness (its own restitution, separate from pegs)
    float pegRadius  = 0.022f;  // DEFAULT peg size for new pegs / makeStaggeredBoard
    float restitution = 0.5f; // middle ground. NOTE: cascade richness still needs by-ear
                              // tuning once audio is wired (the deterministic core is correct;
                              // a near-centered landing can still balance on a peg until the
                              // no-stuck nudge frees it -- see ENG-LAYOUT T1 follow-up).
    float dropX = 0.42f;      // OFF the peg columns/gaps: a perfectly symmetric drop would
                              // balance on the apex peg forever (no asymmetry to deflect it)
    float dropY = 1.33f;
    float initialVx = 0.0f;   // no sideways launch -> every drop is identical (down from dropX);
                              // the off-center dropX already breaks symmetry for a cascade
    float exitY = 0.04f;      // ball below this = exited -> respawn
    float energyFloor = 0.05f;// below this speed AND not contacting a peg = "at rest"
    float nudge = 0.35f;      // GENTLE horizontal velocity kick when genuinely stuck (escalates per try)
    int   stuckSteps = 120;   // sim steps at rest (~0.12s) before nudging. RESET BY ANY PEG CONTACT,
                              // so a decaying "quarter-on-a-table" clatter plays out fully -- only a
                              // truly silent, non-bouncing ball accrues this (no phantom push).
    int   maxNudges = 2;      // gentle nudges before giving up and respawning -> a SHORT timeout once
                              // the ball is genuinely motionless (~0.12s x (maxNudges+1) total)
    double maxLoopSeconds = 8.0; // legacy absolute backstop -- NO LONGER USED; the timeout is now
                              // motion-based (stuck + failed nudges) so a lively run is never cut off

    int   pegCount = 0;
    float pegX[128];
    float pegY[128];
    float pegRest[128];       // per-peg restitution (bounce). > 1.0 = extra energy (bumper-like).
    float pegRad[128];        // per-peg radius (size)
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
    bool addPeg(float x, float y, float rest, int type, float radius);  // false if full
    void movePeg(int i, float x, float y);
    void removePeg(int i);
    void setPegType(int i, int type);
    void setBallBounce(float r);   // live ball restitution
    void setBallSize(float r);     // ball radius (applies on next drop)
    void setDropPoint(float x, float y);   // where the ball starts
    void holdAtDrop();                     // park the ball at the drop point (used while stopped)
    void clearPegs();                      // remove all pegs (ball keeps running)
    void setPegs(const BoardParams& src);  // rebuild ALL pegs from src; the ball is preserved
                                           // (keeps flowing) -- used for bulk edits + undo
    int  pegCount() const { return p_.pegCount; }
    const BoardParams& boardParams() const { return p_; }

private:
    static constexpr double SIM_DT = 1.0 / 1000.0; // fixed 1 kHz sim step
    static constexpr int    SUBSTEPS = 4;

    void stepOnce(std::vector<Collision>& out);
    void respawn();
    void createPegBody(int i);   // build the Box2D body+shape for peg i (type encoded in userData)
    void createBall();           // (re)create the ball fresh at the drop point + initial velocity

    BoardParams p_{};
    uint64_t baseSeed_ = 0;
    Pcg32 rng_{};
    int loop_ = 0;
    double elapsed_ = 0.0;   // total time fed in (accumulated)
    long long steps_ = 0;    // sim steps taken so far (target derived by round-to-nearest)
    double simTime_ = 0.0;
    double loopStart_ = 0.0;
    long long rawBegins_ = 0;   // debug: total raw begin-touch events seen
    int slowCount_ = 0;         // consecutive sim steps the ball has been "at rest"
    int movingCount_ = 0;       // consecutive sim steps the ball has been moving (resets escalation)
    int nudgeTries_ = 0;        // gentle nudges attempted in the current stuck episode
    b2BodyId  pegBody_[128]{};  // per-peg Box2D body (for live move/delete)
    b2ShapeId pegShape_[128]{}; // per-peg shape (for live type change via userData)

    b2WorldId world_{};
    b2BodyId  ball_{};
    b2ShapeId ballShape_{};
    bool      inited_ = false;
};
