#include "PhysicsCore.h"
#include <cmath>
#include <cstdint>

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

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
                p.pegRest[n] = p.restitution;
                p.pegRad[n] = p.pegRadius;
                p.pegType[n] = (r % 2 == 0) ? 0 : 1;  // alternate rows: delay / reverb / delay ...
                ++n;
            }
        }
    }
    p.pegCount = n;
}

void PhysicsWorld::init(uint64_t seed, const BoardParams& params) {
    if (inited_) { b2DestroyWorld(world_); inited_ = false; }  // re-init: free the old world
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
    for (int i = 0; i < p_.pegCount; ++i)
        createPegBody(i);

    createBall();

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
float PhysicsWorld::dbgBallX() { return b2Body_GetPosition(ball_).x; }
void  PhysicsWorld::setGravity(float g) { if (inited_) b2World_SetGravity(world_, b2Vec2{ 0.0f, -g }); }

void PhysicsWorld::createPegBody(int i) {
    b2BodyDef bd = b2DefaultBodyDef();
    bd.position = b2Vec2{ p_.pegX[i], p_.pegY[i] };
    pegBody_[i] = b2CreateBody(world_, &bd);
    b2Circle c = { b2Vec2{ 0.0f, 0.0f }, p_.pegRad[i] };             // per-peg size
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.material.restitution = p_.pegRest[i];                          // >1 = bumper
    sd.enableContactEvents = true;
    sd.userData = (void*)(intptr_t)(p_.pegType[i] + 1);              // non-null = peg; encodes type
    pegShape_[i] = b2CreateCircleShape(pegBody_[i], &sd, &c);
}

void PhysicsWorld::createBall() {
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.isBullet = true;                  // continuous collision: don't tunnel through pegs/walls
    bd.position = b2Vec2{ p_.dropX, p_.dropY };
    ball_ = b2CreateBody(world_, &bd);
    b2Circle c = { b2Vec2{ 0.0f, 0.0f }, p_.ballRadius };
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.material.restitution = p_.ballRest;          // ball's own bounce
    sd.density = 1.0f;
    sd.enableContactEvents = true;                  // needed for the ball-peg contact to report
    ballShape_ = b2CreateCircleShape(ball_, &sd, &c);
    b2Body_SetLinearVelocity(ball_, b2Vec2{ p_.initialVx, 0.0f });  // break symmetry at drop
}

void PhysicsWorld::setBallBounce(float r) {
    p_.ballRest = r;
    if (inited_) b2Shape_SetRestitution(ballShape_, r);   // live
}

void PhysicsWorld::setBallSize(float r) { p_.ballRadius = r; }   // applies on the next drop

bool PhysicsWorld::addPeg(float x, float y, float rest, int type, float radius) {
    if (!inited_ || p_.pegCount >= 128) return false;
    int i = p_.pegCount;
    p_.pegX[i] = x; p_.pegY[i] = y; p_.pegRest[i] = rest; p_.pegType[i] = type; p_.pegRad[i] = radius;
    createPegBody(i);
    p_.pegCount = i + 1;
    return true;
}

void PhysicsWorld::movePeg(int i, float x, float y) {
    if (!inited_ || i < 0 || i >= p_.pegCount) return;
    p_.pegX[i] = x; p_.pegY[i] = y;
    b2Body_SetTransform(pegBody_[i], b2Vec2{ x, y }, b2MakeRot(0.0f));
}

void PhysicsWorld::removePeg(int i) {
    if (!inited_ || i < 0 || i >= p_.pegCount) return;
    b2DestroyBody(pegBody_[i]);
    int last = p_.pegCount - 1;
    if (i != last) {   // swap the last peg into slot i (keeps arrays compact)
        p_.pegX[i] = p_.pegX[last]; p_.pegY[i] = p_.pegY[last];
        p_.pegRest[i] = p_.pegRest[last]; p_.pegRad[i] = p_.pegRad[last]; p_.pegType[i] = p_.pegType[last];
        pegBody_[i] = pegBody_[last]; pegShape_[i] = pegShape_[last];
    }
    p_.pegCount = last;
}

void PhysicsWorld::setPegType(int i, int type) {
    if (!inited_ || i < 0 || i >= p_.pegCount) return;
    p_.pegType[i] = type;
    b2Shape_SetUserData(pegShape_[i], (void*)(intptr_t)(type + 1));
}

void PhysicsWorld::setDropPoint(float x, float y) { p_.dropX = x; p_.dropY = y; }

void PhysicsWorld::clearPegs() {
    if (!inited_) return;
    for (int i = 0; i < p_.pegCount; ++i) b2DestroyBody(pegBody_[i]);
    p_.pegCount = 0;
}

void PhysicsWorld::holdAtDrop() {
    if (!inited_) return;
    b2Body_SetTransform(ball_, b2Vec2{ p_.dropX, p_.dropY }, b2MakeRot(0.0f));
    b2Body_SetLinearVelocity(ball_, b2Vec2{ 0.0f, 0.0f });
    b2Body_SetAngularVelocity(ball_, 0.0f);
}

void PhysicsWorld::respawn() {
    ++loop_;
    // reseed IDENTICALLY each loop so the groove repeats (loop 0 == loop N), and RECREATE the
    // ball fresh (instead of teleporting) so it carries no residual contact/impulse from the
    // exit -- this is what made later drops drift right vs the first.
    rng_.seed(baseSeed_, 1);
    b2DestroyBody(ball_);
    createBall();
    slowCount_ = 0;
    loopStart_ = simTime_;
}

void PhysicsWorld::stepOnce(std::vector<Collision>& out) {
    b2World_Step(world_, (float)SIM_DT, SUBSTEPS);

    b2ContactEvents ce = b2World_GetContactEvents(world_);
    rawBegins_ += ce.beginCount;   // debug
    for (int i = 0; i < ce.beginCount; ++i) {
        void* ua = b2Shape_GetUserData(ce.beginEvents[i].shapeIdA);
        void* ub = b2Shape_GetUserData(ce.beginEvents[i].shapeIdB);
        void* pegUd = ua ? ua : ub;   // non-null userData = the peg shape (walls/ball are null)
        if (!pegUd) continue;         // ball-wall contact -- not a tap
        b2Vec2 pos = b2Body_GetPosition(ball_);
        b2Vec2 vel = b2Body_GetLinearVelocity(ball_);
        Collision c;
        c.t = simTime_;
        c.nx = clamp01(pos.x / p_.width);
        c.ny = clamp01(pos.y / p_.topY);
        c.energy = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        c.loop = loop_;
        c.type = (int)(intptr_t)pegUd - 1;   // userData encodes (type+1): 1->delay, 2->reverb
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

    // Containment: keep the ball inside the frame (left/right/top). Catches any tunneling
    // through the thin walls and reflects it back toward the pegs (a guaranteed wall bounce).
    {
        b2Vec2 cp = b2Body_GetPosition(ball_);
        b2Vec2 cv = b2Body_GetLinearVelocity(ball_);
        bool fix = false;
        if (cp.x < p_.ballRadius)            { cp.x = p_.ballRadius;            if (cv.x < 0) cv.x = -cv.x * 0.5f; fix = true; }
        if (cp.x > p_.width - p_.ballRadius) { cp.x = p_.width - p_.ballRadius; if (cv.x > 0) cv.x = -cv.x * 0.5f; fix = true; }
        if (cp.y > p_.topY - p_.ballRadius)  { cp.y = p_.topY - p_.ballRadius;  if (cv.y > 0) cv.y = -cv.y * 0.5f; fix = true; }
        if (fix) { b2Body_SetTransform(ball_, cp, b2MakeRot(0.0f)); b2Body_SetLinearVelocity(ball_, cv); }
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
