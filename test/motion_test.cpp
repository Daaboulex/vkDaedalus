#include "overlay/motion.hpp"

#include <cmath>
#include <cstdio>

static int failures = 0;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

int main()
{
    using vkBasalt::approach;
    const float tau = 0.05f;

    float fine = 0.0f;
    for (int i = 0; i < 6; i++)
        fine = approach(fine, 1.0f, 1.0f / 240.0f, tau);
    const float coarse = approach(0.0f, 1.0f, 1.0f / 40.0f, tau);
    expect(std::fabs(fine - coarse) < 0.002f, "six frames at 240 Hz reach where one frame at 40 Hz does");

    expect(approach(0.0f, 1.0f, 0.016f, tau) > 0.0f && approach(0.0f, 1.0f, 0.016f, tau) < 1.0f, "one frame moves part of the way");
    expect(approach(0.0f, 1.0f, 2.0f, tau) == 1.0f, "a long pause lands exactly on the target, never past it");
    expect(approach(1.0f, 0.0f, 2.0f, tau) == 0.0f, "fading out lands exactly on zero");

    float settling = 0.0f;
    for (int i = 0; i < 400; i++)
        settling = approach(settling, 1.0f, 1.0f / 60.0f, tau);
    expect(settling == 1.0f, "a hover held for a while settles on the target");

    expect(approach(0.3f, 1.0f, NAN, tau) == 0.3f, "a broken frame time leaves the value where it was");
    expect(approach(0.3f, 1.0f, -0.01f, tau) == 0.3f, "a negative frame time leaves the value where it was");
    expect(approach(0.3f, 1.0f, 0.0f, tau) == 0.3f, "no time passed, no movement");
    expect(approach(NAN, 1.0f, 0.016f, tau) == 1.0f, "a poisoned value recovers to the target");
    expect(approach(0.3f, NAN, 0.016f, tau) == 0.3f, "a broken target is ignored");
    expect(approach(0.3f, 1.0f, 0.016f, 0.0f) == 1.0f, "no time constant snaps to the target");

    if (failures == 0)
        std::printf("motion: every case holds\n");
    return failures == 0 ? 0 : 1;
}
