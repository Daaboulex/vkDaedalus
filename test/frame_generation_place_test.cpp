#include "frame_generation_place.hpp"
#include "latency_stats.hpp"

#include <cstdio>
#include <string>
#include <vector>

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
    using vkBasalt::FrameGenerationPlace;
    const std::string lsfg   = "/nix/store/x-lsfg-vk-2.0.0/lib/liblsfg-vk-layer.so";
    const std::string loader = "/nix/store/y-vulkan-loader/lib/libvulkan.so.1";
    const std::string driver = "/run/opengl-driver/lib/libvulkan_radeon.so";

    expect(vkBasalt::frameGenerationPlace({lsfg, loader}, true) == FrameGenerationPlace::BelowTheLayer,
           "lsfg-vk in the chain below the layer is below it");
    expect(vkBasalt::frameGenerationPlace({loader, driver}, true) == FrameGenerationPlace::AboveTheLayer,
           "lsfg-vk loaded but not below the layer is above it");
    expect(vkBasalt::frameGenerationPlace({loader, driver}, false) == FrameGenerationPlace::NotLoaded,
           "lsfg-vk neither below nor loaded is not loaded");
    expect(vkBasalt::frameGenerationPlace({}, false) == FrameGenerationPlace::NotLoaded, "an empty chain has no lsfg-vk");
    expect(!vkBasalt::isLsfgLayerLibrary("/opt/notlsfg/liblsfg-vk-layer.so.backup/libother.so"),
           "only the file name decides, not a folder named like lsfg-vk's library");
    expect(vkBasalt::isLsfgLayerLibrary("liblsfg-vk-layer.so"), "a bare file name is read");

    expect(vkBasalt::layerLibrariesBelow({lsfg, "/nix/store/v/lib/libVkLayer_khronos_validation.so", loader}) ==
               std::vector<std::string>{"liblsfg-vk-layer.so", "libVkLayer_khronos_validation.so"},
           "the layers below are named by their library's file name, in chain order, the loader at the bottom left out");
    using Steps = std::vector<vkBasalt::PipelineStep>;
    expect(vkBasalt::pipelineSteps(FrameGenerationPlace::BelowTheLayer, true, {1, 2, 2, 1}) ==
               Steps{{"Game", {1}}, {"Effects", {1, 2}}, {"Frame generation", {1}}, {"Display", {1}}},
           "lsfg-vk below: the effects come before frame generation, with each GPU they run on once");
    expect(vkBasalt::pipelineSteps(FrameGenerationPlace::AboveTheLayer, true, {2}) ==
               Steps{{"Game", {1}}, {"Frame generation", {1}}, {"Effects", {2}}, {"Display", {1}}},
           "lsfg-vk above: frame generation comes first, always on the game's GPU");
    expect(vkBasalt::pipelineSteps(FrameGenerationPlace::NotLoaded, false, {}) == Steps{{"Game", {1}}, {"Display", {1}}},
           "no lsfg-vk and no effect leaves the game and the display");
    expect(vkBasalt::pipelineSteps(FrameGenerationPlace::BelowTheLayer, false, {1}) == Steps{{"Game", {1}}, {"Effects", {1}}, {"Display", {1}}},
           "lsfg-vk loaded at 1x generates nothing, so the run's order shows no frame generation, as the card above it says Off");

    expect(!vkBasalt::effectsRunAfterFrameGeneration(FrameGenerationPlace::BelowTheLayer), "lsfg-vk below: effects run before it");
    expect(vkBasalt::effectsRunAfterFrameGeneration(FrameGenerationPlace::AboveTheLayer), "lsfg-vk above: effects run after it");

    const vkBasalt::FrameRates above = vkBasalt::frameRatesOf(240.0, 4u, vkBasalt::effectsRunAfterFrameGeneration(FrameGenerationPlace::AboveTheLayer));
    expect(above.shownFps == 240.0 && above.shownMeasured && above.gameFps == 60.0 && !above.gameMeasured,
           "with lsfg-vk above, the 240 presents the layer times are the shown rate and the game's is 60, estimated");
    const vkBasalt::FrameRates below = vkBasalt::frameRatesOf(60.0, 4u, vkBasalt::effectsRunAfterFrameGeneration(FrameGenerationPlace::BelowTheLayer));
    expect(below.gameFps == 60.0 && below.gameMeasured && below.shownFps == 240.0 && !below.shownMeasured,
           "with lsfg-vk below, the 60 presents the layer times are the game's and 240 are shown, estimated");

    return failures == 0 ? 0 : 1;
}
