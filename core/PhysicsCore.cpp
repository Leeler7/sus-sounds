#include "PhysicsCore.h"
#include <cmath>
#include <cstdint>

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// The fastest the ball could ever go on gravity alone: free-fall through the board height.
// We cap the ball's speed here so bounce > 1 (powered bumpers) can never push it past the
// scale the gravity knob sets -- no low-gravity runaway.
static inline float speedCapForGravity(float g, float topY) {
    float v = std::sqrt(2.0f * (g > 0.0f ? g : 0.0f) * topY);
    return v < 0.1f ? 0.1f : v;
}

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
    movingCount_ = 0;
    nudgeTries_ = 0;
    rng_.seed(seed, 1);

    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = b2Vec2{ 0.0f, -p_.gravity };
    // Box2D ignores restitution below this CLOSING speed (anti-jitter). Default is 1.0, but our
    // whole board is ~1 unit wide, so at low gravity slow contacts never bounced. Drop it low so
    // bounce behaves consistently at any gravity.
    wd.restitutionThreshold = 0.05f;
    // Cap speed at the gravity-set free-fall speed (default is 400 = effectively no cap). Bounce > 1
    // adds energy and compounds hit-over-hit; without this it runs away, worst at LOW gravity where
    // the runaway speed dwarfs what the ball should be doing. Tying the cap to gravity fixes that.
    wd.maximumLinearSpeed = speedCapForGravity(p_.gravity, p_.topY);
    world_ = b2CreateWorld(&wd);

    createWalls();

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
void  PhysicsWorld::setGravity(float g) {
    p_.gravity = g;
    if (inited_) {
        b2World_SetGravity(world_, b2Vec2{ 0.0f, -g });
        b2World_SetMaximumLinearSpeed(world_, speedCapForGravity(g, p_.topY));  // keep the cap tied to gravity
    }
}

void PhysicsWorld::createWalls() {
    // Side walls: static segments, NO contact events (wall bounces are not taps).
    b2BodyDef bd = b2DefaultBodyDef();
    wallBody_ = b2CreateBody(world_, &bd);
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.material.restitution = p_.restitution;
    float half = p_.width * 0.5f, xMin = kBoardCenterX - half, xMax = kBoardCenterX + half;
    b2Segment left  = { b2Vec2{ xMin, 0.0f }, b2Vec2{ xMin, p_.topY } };
    b2Segment right = { b2Vec2{ xMax, 0.0f }, b2Vec2{ xMax, p_.topY } };
    b2CreateSegmentShape(wallBody_, &sd, &left);
    b2CreateSegmentShape(wallBody_, &sd, &right);
}

void PhysicsWorld::setWidth(float w) {
    if (w < 0.3f) w = 0.3f;                              // floor: keep the board playable
    if (!inited_) { p_.width = w; return; }
    if (std::fabs(w - p_.width) < 1e-4f) return;         // unchanged
    p_.width = w;
    b2DestroyBody(wallBody_);                            // rebuild the walls at the new width (ball kept)
    createWalls();
    // keep the drop point inside the new (centered) bounds so the ball never spawns outside a wall
    float half = w * 0.5f, xMin = kBoardCenterX - half, xMax = kBoardCenterX + half;
    if (p_.dropX > xMax - p_.ballRadius) p_.dropX = xMax - p_.ballRadius;
    if (p_.dropX < xMin + p_.ballRadius) p_.dropX = xMin + p_.ballRadius;
}

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

void PhysicsWorld::setBallSize(float r) {
    if (!inited_) { p_.ballRadius = r; return; }
    if (std::fabs(r - p_.ballRadius) < 1e-5f) return;   // unchanged
    p_.ballRadius = r;
    b2DestroyShape(ballShape_, true);                   // resize live (keeps body pos/velocity)
    b2Circle c = { b2Vec2{ 0.0f, 0.0f }, r };
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.material.restitution = p_.ballRest;
    sd.density = 1.0f;
    sd.enableContactEvents = true;
    ballShape_ = b2CreateCircleShape(ball_, &sd, &c);
}

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

void PhysicsWorld::setPegs(const BoardParams& src) {
    // Copy just the peg data (positions/sizes/bounce/types); leave the ball, walls, gravity,
    // drop point, RNG and loop state alone so a bulk edit doesn't restart the groove.
    auto copyPegs = [&] {
        p_.pegCount = src.pegCount;
        for (int i = 0; i < src.pegCount; ++i) {
            p_.pegX[i] = src.pegX[i];   p_.pegY[i] = src.pegY[i];
            p_.pegRest[i] = src.pegRest[i]; p_.pegRad[i] = src.pegRad[i];
            p_.pegType[i] = src.pegType[i];
        }
    };
    if (!inited_) { copyPegs(); return; }
    for (int i = 0; i < p_.pegCount; ++i) b2DestroyBody(pegBody_[i]);  // ball untouched
    copyPegs();
    for (int i = 0; i < p_.pegCount; ++i) createPegBody(i);
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
    movingCount_ = 0;
    nudgeTries_ = 0;
    loopStart_ = simTime_;
}

void PhysicsWorld::stepOnce(std::vector<Collision>& out) {
    b2World_Step(world_, (float)SIM_DT, SUBSTEPS);

    const float half = p_.width * 0.5f;
    const float xMin = kBoardCenterX - half, xMax = kBoardCenterX + half;

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
        c.nx = clamp01((pos.x - xMin) / p_.width);   // 0 = left wall, 1 = right wall (for pan)
        c.ny = clamp01(pos.y / p_.topY);
        c.energy = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        c.loop = loop_;
        c.type = (int)(intptr_t)pegUd - 1;   // userData encodes (type+1): 1->delay, 2->reverb
        out.push_back(c);
    }

    // No-stuck protocol: "at rest" means slow AND not contacting a peg this step. Any contact
    // (a bounce/clatter) resets the timer, so a decaying clatter plays out fully -- only a truly
    // silent, balanced ball accrues rest. Then a GENTLE, slightly-escalating horizontal kick;
    // if a couple of nudges don't free it, a SHORT timeout respawns. Lively balls are never touched.
    bool stuckTimeout = false;
    bool contacting = (ce.beginCount > 0);   // still bouncing/clattering this step?
    b2Vec2 v = b2Body_GetLinearVelocity(ball_);
    float speed = std::sqrt(v.x * v.x + v.y * v.y);
    if (speed < p_.energyFloor && !contacting) { ++slowCount_; movingCount_ = 0; }
    else { slowCount_ = 0; if (++movingCount_ > p_.stuckSteps) nudgeTries_ = 0; }  // freed -> fresh tries
    if (slowCount_ >= p_.stuckSteps) {
        if (nudgeTries_ < p_.maxNudges) {
            float mass = b2Body_GetMass(ball_);
            float mag  = p_.nudge * (1.0f + 0.5f * (float)nudgeTries_);   // gentle, escalates each try
            float dir  = rng_.nextSigned();
            b2Body_ApplyLinearImpulseToCenter(ball_, b2Vec2{ mass * mag * dir, 0.0f }, true);
            ++nudgeTries_;
            slowCount_ = 0;
        } else {
            stuckTimeout = true;   // nudges exhausted and still at rest -> time out (respawn)
        }
    }

    // Containment: keep the ball inside the frame (left/right/top). Catches any tunneling
    // through the thin walls and reflects it back toward the pegs (a guaranteed wall bounce).
    {
        b2Vec2 cp = b2Body_GetPosition(ball_);
        b2Vec2 cv = b2Body_GetLinearVelocity(ball_);
        bool fix = false;
        if (cp.x < xMin + p_.ballRadius) { cp.x = xMin + p_.ballRadius; if (cv.x < 0) cv.x = -cv.x * 0.5f; fix = true; }
        if (cp.x > xMax - p_.ballRadius) { cp.x = xMax - p_.ballRadius; if (cv.x > 0) cv.x = -cv.x * 0.5f; fix = true; }
        if (cp.y > p_.topY - p_.ballRadius)  { cp.y = p_.topY - p_.ballRadius;  if (cv.y > 0) cv.y = -cv.y * 0.5f; fix = true; }
        if (fix) { b2Body_SetTransform(ball_, cp, b2MakeRot(0.0f)); b2Body_SetLinearVelocity(ball_, cv); }
    }

    simTime_ += SIM_DT;

    // Respawn ONLY on a real exit (fell past the bottom) or when genuinely stuck despite nudges.
    // No motion-independent timeout -- a lively run keeps going.
    b2Vec2 pos = b2Body_GetPosition(ball_);
    if (pos.y <= p_.exitY || stuckTimeout)
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
