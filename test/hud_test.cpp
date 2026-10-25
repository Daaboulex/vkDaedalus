#include "overlay/hud.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace vkBasalt;

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

int main()
{
    expect(hudItemsFrom("effects,fps") == std::vector<HudItem>{HudItem::Fps, HudItem::EffectsTime} && hudItemsWord({HudItem::EffectsTime, HudItem::Fps}) == "fps,effects",
           "items are read in any order and written in one, so a file reads back as it was saved");
    expect(!hudItemsFrom("fps,gpu") && !hudItemsFrom("") && !hudItemsFrom("fps,"), "an unknown item, an empty list or a trailing comma is refused, so the default stays");
    expect(hudPresets().front().items == std::vector<HudItem>{HudItem::Fps}, "the first preset, the default, shows FPS only");
    const HudWindows fpsOnly  = hudWindowsFor({HudItem::Fps});
    const HudWindows detailed = hudWindowsFor({HudItem::Fps, HudItem::FrameTime, HudItem::SlowestOnePercent, HudItem::EffectsTime});
    const HudWindows effects  = hudWindowsFor({HudItem::EffectsTime});
    expect(fpsOnly.lastSecond && !fpsOnly.lastTenSeconds, "FPS only needs the last second and never sorts ten seconds of frames");
    expect(detailed.lastSecond && detailed.lastTenSeconds, "the detailed HUD needs both windows");
    expect(!effects.lastSecond && !effects.lastTenSeconds, "effects time alone needs no frame window");

    const FrameStats second = frameStats(std::vector<double>(60, 1000.0 / 60.0));
    const FrameStats ten    = frameStats(std::vector<double>{16.0, 16.0, 50.0});
    expect(hudLines({HudItem::Fps}, second, ten, std::nullopt, false, std::nullopt) == std::vector<std::string>{"60 FPS"},
           "FPS only shows one line, and no shown rate while lsfg-vk does not generate");
    expect(hudLines({HudItem::Fps}, second, ten, 2u, false, std::nullopt) == std::vector<std::string>{"60 FPS", "120 shown"},
           "while lsfg-vk generates at 2x before the effects, the game's rate and the shown rate both show");
    expect(hudLines({HudItem::Fps}, second, ten, 2u, true, std::nullopt) == std::vector<std::string>{"30 FPS", "60 shown"},
           "with the effects after frame generation the timed rate is the shown one and the game's is it divided by the multiplier");
    expect(hudLines({HudItem::Fps, HudItem::FrameTime, HudItem::SlowestOnePercent, HudItem::EffectsTime}, second, ten, std::nullopt, false, 0.25) ==
               std::vector<std::string>{"60 FPS", "16.7 ms", "1% 50.0 ms", "effects 0.25 ms"},
           "the detailed lines follow the items' order");
    expect(hudLines({HudItem::Fps, HudItem::EffectsTime}, FrameStats{}, ten, std::nullopt, false, std::nullopt).empty(),
           "no frame in the last second and no effects time leave nothing to show rather than a zero");
    return failures ? 1 : 0;
}
