/*
 * T0a -- cross-platform determinism harness (Box2D v3).
 *
 * Drops one ball through a fixed peg layout and prints the ball's position
 * every step as EXACT hex floats (%a). Run this on Windows, macOS, and Linux,
 * save each output, and diff them. Byte-identical across all three => Box2D v3
 * is cross-platform deterministic for our use, and the "shared boards sound
 * identical everywhere" promise holds. ANY difference => the promise is at risk
 * and you learned it in a day, before building the engine on top of it.
 *
 * NOTE: written against the Box2D v3 C API from memory. Before trusting it,
 * sanity-check the symbol names against the version CMake fetches (GIT_TAG in
 * CMakeLists.txt). The v3 API is stable but may have drifted slightly; adjust
 * names if the build complains. The METHOD (dump hex-float trajectory, diff
 * across OSes) is what matters and is API-independent.
 */
#include <box2d/box2d.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = (b2Vec2){0.0f, -10.0f};
    b2WorldId world = b2CreateWorld(&worldDef);

    /* fixed staggered pegs (static bodies) */
    const float pegX[] = {-0.30f, 0.00f, 0.30f, -0.15f, 0.15f};
    const float pegY[] = { 2.00f, 1.70f, 2.00f,  1.40f, 1.40f};
    for (int i = 0; i < 5; ++i) {
        b2BodyDef bd = b2DefaultBodyDef();          /* static by default */
        bd.position = (b2Vec2){pegX[i], pegY[i]};
        b2BodyId peg = b2CreateBody(world, &bd);
        b2Circle c = {(b2Vec2){0.0f, 0.0f}, 0.08f};
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.material.restitution = 0.8f;     /* v3.1: restitution lives in .material */
        b2CreateCircleShape(peg, &sd, &c);
    }

    /* one dynamic ball at a slightly off-center drop point */
    b2BodyDef bbd = b2DefaultBodyDef();
    bbd.type = b2_dynamicBody;
    bbd.position = (b2Vec2){0.02f, 2.60f};
    b2BodyId ball = b2CreateBody(world, &bbd);
    b2Circle bc = {(b2Vec2){0.0f, 0.0f}, 0.06f};
    b2ShapeDef bsd = b2DefaultShapeDef();
    bsd.material.restitution = 0.8f;        /* v3.1: restitution lives in .material */
    bsd.density = 1.0f;
    b2CreateCircleShape(ball, &bsd, &bc);

    const float dt = 1.0f / 1200.0f;
    const int subSteps = 4;
    for (int step = 0; step < 3000; ++step) {
        b2World_Step(world, dt, subSteps);
        b2Vec2 p = b2Body_GetPosition(ball);
        /* Print the RAW IEEE-754 bit patterns as fixed %08x. This is identical
           text on every platform iff the float values are bit-identical. Do NOT
           use %a: the digit count it emits is implementation-defined (MSVC pads,
           glibc trims), which would make identical physics hash differently. */
        uint32_t bx, by;
        memcpy(&bx, &p.x, sizeof bx);
        memcpy(&by, &p.y, sizeof by);
        printf("%d %08x %08x\n", step, bx, by);
    }

    b2DestroyWorld(world);
    return 0;
}
