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
    expect(hudLines({HudItem::Fps}, second, ten, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt) == std::vector<std::string>{"60 FPS"},
           "FPS only shows one line, and no shown rate while lsfg-vk does not generate");
    expect(hudLines({HudItem::Fps}, second, ten, 2u, std::nullopt, false, std::nullopt, std::nullopt) == std::vector<std::string>{"60 FPS", "~120 shown"},
           "while lsfg-vk generates at 2x before the effects with nothing counted below it, the game's rate and the shown rate both show, the shown one marked estimated");
    expect(hudLines({HudItem::Fps}, second, ten, 2u, std::nullopt, true, std::nullopt, std::nullopt) == std::vector<std::string>{"~30 FPS", "60 shown"},
           "with the effects after frame generation the timed rate is the shown one and the game's is it divided by the multiplier, marked estimated");
    expect(hudLines({HudItem::Fps}, second, ten, std::nullopt, AdaptiveTarget{90, false, 3}, false, std::nullopt, std::nullopt)
               == std::vector<std::string>{"60 FPS", "~90 shown", "adaptive to 90 FPS"},
           "with MAKO in Adaptive before the effects, the game's rate shows with the frames shown, estimated at the target its ceiling reaches, and the target");
    expect(hudLines({HudItem::Fps}, second, ten, std::nullopt, AdaptiveTarget{238, false, 3}, false, std::nullopt, std::nullopt)
               == std::vector<std::string>{"60 FPS", "~180 shown", "adaptive to 238 FPS"},
           "a target above the game's rate times Max multiplier is not shown: the ceiling decides, as at a fixed multiplier");
    expect(hudLines({HudItem::Fps}, second, ten, std::nullopt, AdaptiveTarget{90, false, 3}, true, std::nullopt, std::nullopt) == std::vector<std::string>{"60 shown", "adaptive to 90 FPS"},
           "with the effects after MAKO in Adaptive the timed rate is the frames shown, and the game's own is not worked out");
    expect(hudLines({HudItem::Fps}, second, ten, std::nullopt, AdaptiveTarget{90, true, 3}, false, std::nullopt, std::nullopt) == std::vector<std::string>{"60 FPS", "adaptive to the refresh"},
           "Adaptive aimed at a display refresh the layer does not know shows no shown rate rather than a guess");
    expect(hudLines({HudItem::Fps, HudItem::FrameTime, HudItem::SlowestOnePercent, HudItem::EffectsTime}, second, ten, std::nullopt, std::nullopt, false, std::nullopt, 0.25) ==
               std::vector<std::string>{"60 FPS", "16.7 ms", "1% 50.0 ms", "effects 0.25 ms"},
           "the detailed lines follow the items' order");
    expect(hudLines({HudItem::Fps, HudItem::EffectsTime}, FrameStats{}, ten, std::nullopt, std::nullopt, false, std::nullopt, std::nullopt).empty(),
           "no frame in the last second and no effects time leave nothing to show rather than a zero");
    expect(hudLines({HudItem::Fps}, second, ten, std::nullopt, AdaptiveTarget{238, false, 4}, false, 248.0, std::nullopt)
               == std::vector<std::string>{"60 FPS", "248 shown", "adaptive to 238 FPS"},
           "with the presents counted below MAKO, the HUD shows that count as the frames shown, unmarked, instead of the estimate");
    expect(hudLines({HudItem::Fps}, second, ten, 3u, std::nullopt, false, 171.0, std::nullopt) == std::vector<std::string>{"60 FPS", "171 shown"},
           "a fixed multiplier falling short, counted below frame generation, shows what reached the display");
    return failures ? 1 : 0;
}
