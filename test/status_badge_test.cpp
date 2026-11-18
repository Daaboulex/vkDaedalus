#include "overlay/status_badge.hpp"

#include <cstdio>
#include <optional>
#include <string>

using namespace vkBasalt;

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

static bool is(const Badge& badge, Tone tone, std::optional<Icon> icon, const std::string& text)
{
    return badge.tone == tone && badge.icon == icon && badge.text == text;
}

static void frameGenerationHeader()
{
    FrameGenerationHeader running;
    running.amount = "Adaptive";
    running.on     = true;
    running.loaded = true;
    running.live   = true;
    expect(is(frameGenerationBadge(running), Tone::Ok, Icon::Power, "Adaptive, on"), "frame generation that runs now is green with the power icon");

    FrameGenerationHeader off = running;
    off.on                    = false;
    expect(is(frameGenerationBadge(off), Tone::Muted, Icon::Power, "Off"), "frame generation switched off is grey with the power icon");
    off.loaded = false;
    off.live   = false;
    expect(is(frameGenerationBadge(off), Tone::Muted, Icon::Power, "Off, not loaded"), "off with the layer not loaded is grey and says so");

    FrameGenerationHeader paused = running;
    paused.amount                = "3x";
    paused.paused                = true;
    expect(is(frameGenerationBadge(paused), Tone::Warn, Icon::Pause, "3x, paused"), "frame generation MAKO pauses is amber with the pause icon");

    FrameGenerationHeader model = paused;
    model.needsModel            = true;
    expect(is(frameGenerationBadge(model), Tone::Bad, Icon::Warning, "3x, needs a model"), "a missing model is a red fault with the alert, before any pause");

    FrameGenerationHeader later = running;
    later.amount                = "3x";
    later.live                  = false;
    later.loadsNextLaunch       = true;
    expect(is(frameGenerationBadge(later), Tone::Warn, Icon::Restart, "3x, next launch"), "on and waiting for the next launch is amber with the restart icon");
    later.loadsNextLaunch = false;
    expect(is(frameGenerationBadge(later), Tone::Warn, Icon::Warning, "3x, needs vkdaedalus-run"), "on and waiting for vkdaedalus-run is amber with the alert");

    FrameGenerationHeader blocked = running;
    blocked.blocked               = true;
    expect(is(frameGenerationBadge(blocked), Tone::Muted, Icon::Power, "Unavailable"), "a blocker wins over on, grey");
    blocked.notInstalled = true;
    expect(is(frameGenerationBadge(blocked), Tone::Muted, Icon::Power, "Not installed"), "a layer not installed is grey and says so");
}

static void scalingHeader()
{
    MakoScalingShown scaling;
    scaling.run    = MakoScalingRun::Scaling;
    scaling.detail = "1280x720 to 2560x1440";
    expect(is(scalingBadge(false, scaling, "MAKO Scaler", "MAKO Scaler 2x", true), Tone::Ok, Icon::Power, "MAKO Scaler 1280x720 to 2560x1440, on"),
           "scaling MAKO reports running is green with the method and sizes it runs");
    expect(is(scalingBadge(true, scaling, "MAKO Scaler", "MAKO Scaler 2x", true), Tone::Muted, Icon::Power, "Unavailable"), "a blocker wins over scaling, grey");
    scaling.run = MakoScalingRun::NotScaling;
    expect(is(scalingBadge(false, scaling, "", "MAKO Scaler 2x", true), Tone::Warn, Icon::Pause, "Not scaling"), "scaling MAKO reports inactive is amber with the pause icon");
    scaling.run = MakoScalingRun::NoStatus;
    expect(is(scalingBadge(false, scaling, "", "MAKO Scaler 2x", true), Tone::Warn, Icon::Warning, "No status file"), "no status file is amber with the alert");
    scaling.run = MakoScalingRun::Off;
    expect(is(scalingBadge(false, scaling, "", "MAKO Scaler 2x", true), Tone::Muted, Icon::Power, "Off"), "scaling off is grey with the power icon");
    scaling.run = MakoScalingRun::NotStarted;
    expect(is(scalingBadge(false, scaling, "", "MAKO Scaler 2x", true), Tone::Warn, Icon::Restart, "MAKO Scaler 2x, next launch"),
           "scaling on that starts at the next launch is amber with the restart icon");
    expect(is(scalingBadge(false, scaling, "", "MAKO Scaler 2x", false), Tone::Warn, Icon::Warning, "MAKO Scaler 2x, needs vkdaedalus-run"),
           "scaling on that waits for vkdaedalus-run is amber with the alert");
}

static void makoGpu()
{
    expect(is(makoGpuClaimBadge({true, {1}, "", ""}), Tone::Ok, Icon::Power, "Generating on GPU 1"), "MAKO generating on the game's GPU is green and names GPU 1");
    expect(is(makoGpuClaimBadge({true, {2}, "0xABCD:0x2684", ""}), Tone::Ok, Icon::Power, "Generating on GPU 2"), "MAKO generating on another GPU is green and names it");
    expect(is(makoGpuClaimBadge({false, {3}, "0xABCD:0x2684", "failed to create swapchain context"}), Tone::Bad, Icon::Warning, "No frames on GPU 3"),
           "MAKO naming a GPU it generates no frames on is a red fault naming that GPU");
}

static void nextLaunch()
{
    expect(is(changesWaitingBadge(1), Tone::Warn, Icon::Warning, "1 change at next launch"), "one waiting change is an amber alert in the singular");
    expect(is(changesWaitingBadge(3), Tone::Warn, Icon::Warning, "3 changes at next launch"), "several waiting changes are counted");
    const std::optional<Badge> waiting = nextLaunchBadge(true);
    expect(waiting && is(*waiting, Tone::Warn, Icon::Warning, "Next launch"), "a row whose saved value differs from what runs shows the amber Next launch alert");
    expect(!nextLaunchBadge(false), "a row whose saved value is what runs shows nothing");
}

static void shaderTest()
{
    expect(is(allowedBadge(4), Tone::Ok, Icon::Shield, "4 allowed"), "allowed shaders are green with the shield");
    expect(is(failedBadge(0), Tone::Muted, std::nullopt, "0 failed"), "no failure is a grey tag with no alert");
    expect(is(failedBadge(2), Tone::Bad, Icon::Warning, "2 failed"), "failures are a red fault with the alert");
    expect(is(depthBadge(3, true), Tone::Info, Icon::Shield, "3 blocked"), "shaders Safe Anti-Cheat blocks carry the shield");
    expect(is(depthBadge(1, false), Tone::Info, Icon::Eye, "1 read depth"), "shaders that read depth carry the eye while nothing blocks them");
    expect(is(warningsBadge(1), Tone::Warn, Icon::Warning, "1 warning") && is(warningsBadge(5), Tone::Warn, Icon::Warning, "5 warnings"),
           "a shader's compiler warnings are an amber alert, counted");
}

int main()
{
    frameGenerationHeader();
    scalingHeader();
    makoGpu();
    nextLaunch();
    shaderTest();
    if (failures)
        std::printf("%d status badge check(s) failed\n", failures);
    return failures ? 1 : 0;
}
