#include "PhysicsCore.h"
#include <cmath>

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Tag used to mark peg shapes so a contact event can be identified as a peg hit
// (vs a wall), independent of Box2D's enable-flag semantics. Its address is the marker.
static int PEG_MARKER = 1;

void makeStaggeredBoard(BoardParams& p, int rows, int cols) {
    int n = 0;
    for (int r = 0; r < rows && n < 128; ++r) {
        float y = 0.20f + r * (p.topY - 0.35f) / rows;
        float off = (r % 2 == 0) ? 0.0f : 0.5f;
        for (int c = 0; c < cols && n < 128; ++c) {
            float x = (c + off + 0.5f) * (p.width / cols);
            if (x > 0.06f && x < p.width - 0.06f) {
                p.pegX[n] = x;
                p.pegY[n] = y;
                p.pegRest[n] = p.restitution;  // default; caller can raise individual pegs to bumpers
                ++n;
            }
        }
    }
    p.pegCount = n;
}

void PhysicsWorld::init(uint64_t seed, const BoardParams& params) {
    p_ = params;
    baseSeed_ = seed;
    loop_ = 0;
    elapsed_ = 0.0;
    steps_ = 0;
    simTime_ = 0.0;
    loopStart_ = 0.0;
    rawBegins_ = 0;
    slowCount_ = 0;
    rng_.seed(seed, 1);

    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = b2Vec2{ 0.0f, -p_.gravity };
    world_ = b2CreateWorld(&wd);

    // Side walls: static segments, NO contact events (wall bounces are not taps).
    {
        b2BodyDef bd = b2DefaultBodyDef();
        b2BodyId walls = b2CreateBody(world_, &bd);
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.material.restitution = p_.restitution;
        b2Segment left  = { b2Vec2{ 0.0f, 0.0f },      b2Vec2{ 0.0f, p_.topY } };
        b2Segment right = { b2Vec2{ p_.width, 0.0f },  b2Vec2{ p_.width, p_.topY } };
        b2CreateSegmentShape(walls, &sd, &left);
        b2CreateSegmentShape(walls, &sd, &right);
    }

    // Pegs: static circles WITH contact events on (ball-peg contact -> tap event).
    for (int i = 0; i < p_.pegCount; ++i) {
        b2BodyDef bd = b2DefaultBodyDef();
        bd.position = b2Vec2{ p_.pegX[i], p_.pegY[i] };
        b2BodyId peg = b2CreateBody(world_, &bd);
        b2Circle c = { b2Vec2{ 0.0f, 0.0f }, p_.pegRadius };
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.material.restitution = p_.pegRest[i];  // per-peg: > 1.0 = bumper (extra energy)
        sd.enableContactEvents = true;
        sd.userData = &PEG_MARKER;   // tag: this is a peg
        b2CreateCircleShape(peg, &sd, &c);
    }

    // Ball: dynamic circle. Contact events left off here; OR-semantics still fire ball-peg.
    {
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = b2Vec2{ p_.dropX, p_.dropY };
        ball_ = b2CreateBody(world_, &bd);
        b2Circle c = { b2Vec2{ 0.0f, 0.0f }, p_.ballRadius };
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.material.restitution = p_.restitution;
        sd.density = 1.0f;
        sd.enableContactEvents = true;   // needed for the ball-peg contact to report
        b2CreateCircleShape(ball_, &sd, &c);
    }
    b2Body_SetLinearVelocity(ball_, b2Vec2{ p_.initialVx, 0.0f }); // break symmetry at drop

    inited_ = true;
}

void PhysicsWorld::shutdown() {
    if (inited_) {
        b2DestroyWorld(world_);
        world_ = b2WorldId{};
        inited_ = false;
    }
}

PhysicsWorld::~PhysicsWorld() { shutdown(); }

float PhysicsWorld::dbgBallY() { return b2Body_GetPosition(ball_).y; }

void PhysicsWorld::respawn() {
    ++loop_;
    // reseed deterministically from (baseSeed, loopIndex) so each loop is reproducible
    rng_.seed(baseSeed_, (uint64_t)loop_ + 1u);
    b2Body_SetTransform(ball_, b2Vec2{ p_.dropX, p_.dropY }, b2MakeRot(0.0f));
    b2Body_SetLinearVelocity(ball_, b2Vec2{ p_.initialVx, 0.0f }); // break symmetry at respawn
    b2Body_SetAngularVelocity(ball_, 0.0f);
    slowCount_ = 0;
    loopStart_ = simTime_;
}

void PhysicsWorld::stepOnce(std::vector<Collision>& out) {
    b2World_Step(world_, (float)SIM_DT, SUBSTEPS);

    b2ContactEvents ce = b2World_GetContactEvents(world_);
    rawBegins_ += ce.beginCount;   // debug
    for (int i = 0; i < ce.beginCount; ++i) {
        b2ShapeId sa = ce.beginEvents[i].shapeIdA;
        b2ShapeId sb = ce.beginEvents[i].shapeIdB;
        bool pegHit = (b2Shape_GetUserData(sa) == &PEG_MARKER) ||
                      (b2Shape_GetUserData(sb) == &PEG_MARKER);
        if (!pegHit) continue;   // ignore wall contacts -- only pegs become taps
        b2Vec2 pos = b2Body_GetPosition(ball_);
        b2Vec2 vel = b2Body_GetLinearVelocity(ball_);
        Collision c;
        c.t = simTime_;
        c.nx = clamp01(pos.x / p_.width);
        c.ny = clamp01(pos.y / p_.topY);
        c.energy = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        c.loop = loop_;
        out.push_back(c);
    }

    // No-stuck: only when the ball has been slow for a sustained stretch (genuinely
    // stuck -- not at the initial drop or at a bounce apex). Apply a small, mass-scaled,
    // purely horizontal seeded kick so it can't launch the ball out of the board.
    b2Vec2 v = b2Body_GetLinearVelocity(ball_);
    float speed = std::sqrt(v.x * v.x + v.y * v.y);
    if (speed < p_.energyFloor) ++slowCount_; else slowCount_ = 0;
    if (slowCount_ >= p_.stuckSteps) {
        float mass = b2Body_GetMass(ball_);
        float dir = rng_.nextSigned();
        b2Body_ApplyLinearImpulseToCenter(ball_, b2Vec2{ mass * p_.nudge * dir, 0.0f }, true);
        slowCount_ = 0;
    }

    simTime_ += SIM_DT;

    // Exit (fell past the bottom) or hard timeout -> respawn.
    b2Vec2 pos = b2Body_GetPosition(ball_);
    if (pos.y <= p_.exitY || (simTime_ - loopStart_) >= p_.maxLoopSeconds)
        respawn();
}

void PhysicsWorld::advance(double seconds, std::vector<Collision>& out) {
    if (!inited_) return;
    elapsed_ += seconds;
    // Round-to-nearest target: makes block-size invariant (advancing the same total
    // time in one big chunk or many small chunks yields the same step count, absorbing
    // float accumulation drift below half a step).
    long long target = (long long)std::llround(elapsed_ / SIM_DT);
    while (steps_ < target) {
        stepOnce(out);
        ++steps_;
    }
}
