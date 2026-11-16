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

    const std::string mako = "/usr/lib/libmako-render.so";
    expect(vkBasalt::frameGenerationPlace({lsfg, loader}, true, vkBasalt::lsfgLayerLibrary) == FrameGenerationPlace::BelowTheLayer,
           "lsfg-vk in the chain below the layer is below it");
    expect(vkBasalt::frameGenerationPlace({mako, loader}, true, vkBasalt::makoLayerLibrary) == FrameGenerationPlace::BelowTheLayer,
           "MAKO in the chain below the layer is below it");
    expect(vkBasalt::frameGenerationPlace({lsfg, loader}, true, vkBasalt::makoLayerLibrary) == FrameGenerationPlace::AboveTheLayer,
           "MAKO loaded with only lsfg-vk below the layer is above it");
    expect(vkBasalt::frameGenerationPlaceWords(FrameGenerationPlace::BelowTheLayer, "MAKO") == "MAKO runs below the layer: effects before frame generation" &&
               vkBasalt::frameGenerationPlaceWords(FrameGenerationPlace::AboveTheLayer, "lsfg-vk") == "lsfg-vk runs above the layer: effects after frame generation",
           "the log names the layer that generates the frames");
    using vkBasalt::FrameGenerationLayer;
    expect(vkBasalt::defaultFrameGenerationLayer(false, false, false) == FrameGenerationLayer::LsfgVk &&
               vkBasalt::defaultFrameGenerationLayer(true, false, false) == FrameGenerationLayer::LsfgVk &&
               vkBasalt::defaultFrameGenerationLayer(false, true, false) == FrameGenerationLayer::Mako &&
               vkBasalt::defaultFrameGenerationLayer(true, true, false) == FrameGenerationLayer::LsfgVk,
           "a profile with no choice runs the layer that is installed, lsfg-vk when both are or neither is");
    expect(vkBasalt::defaultFrameGenerationLayer(true, true, true) == FrameGenerationLayer::Mako,
           "and MAKO when both are and the launch options set ENABLE_MAKO=1 without DISABLE_MAKO, as they did before the layer choice");
    expect(vkBasalt::defaultFrameGenerationLayer(true, false, true) == FrameGenerationLayer::LsfgVk, "but not when MAKO is not installed");
    expect(vkBasalt::frameGenerationLayerNamed(vkBasalt::frameGenerationLayerName(FrameGenerationLayer::Mako)) == FrameGenerationLayer::Mako &&
               vkBasalt::frameGenerationLayerNamed(vkBasalt::frameGenerationLayerName(FrameGenerationLayer::LsfgVk)) == FrameGenerationLayer::LsfgVk &&
               !vkBasalt::frameGenerationLayerNamed("MAKO") && vkBasalt::frameGenerationLayerName(FrameGenerationLayer::Mako) == "mako",
           "each layer reads back by its name, and no other name is a layer");
    expect(vkBasalt::loadedFrameGenerationLayerOf(true, true) == FrameGenerationLayer::Mako && vkBasalt::loadedFrameGenerationLayerOf(false, true) == FrameGenerationLayer::Mako
               && vkBasalt::loadedFrameGenerationLayerOf(true, false) == FrameGenerationLayer::LsfgVk && !vkBasalt::loadedFrameGenerationLayerOf(false, false),
           "with both layers mapped MAKO is the one loaded, as the log, the HUD and the run's order name it");
    expect(!vkBasalt::layerChangeWaits(vkBasalt::loadedFrameGenerationLayerOf(true, true), FrameGenerationLayer::Mako)
               && vkBasalt::layerChangeWaits(vkBasalt::loadedFrameGenerationLayerOf(true, true), FrameGenerationLayer::LsfgVk)
               && !vkBasalt::layerChangeWaits(std::nullopt, FrameGenerationLayer::Mako),
           "so with both mapped and the profile on MAKO no layer change waits for the next launch, in the card's list and at its Layer mark alike");
    expect(!vkBasalt::loadedAtNextLaunch(FrameGenerationLayer::Mako, false, false),
           "MAKO loads only with ENABLE_MAKO=1, which vkdaedalus-run sets, so a game started without either never runs MAKO, at its next launch either");
    expect(vkBasalt::loadedAtNextLaunch(FrameGenerationLayer::Mako, true, false) && vkBasalt::loadedAtNextLaunch(FrameGenerationLayer::Mako, false, true),
           "a game vkdaedalus-run started, or one started with ENABLE_MAKO=1, runs MAKO from its next launch");
    expect(vkBasalt::loadedAtNextLaunch(FrameGenerationLayer::LsfgVk, false, false), "lsfg-vk joins every game at its next launch unless a blocker keeps it out");

    expect(vkBasalt::frameGenerationPlace({loader, driver}, true, vkBasalt::lsfgLayerLibrary) == FrameGenerationPlace::AboveTheLayer,
           "lsfg-vk loaded but not below the layer is above it");
    expect(vkBasalt::frameGenerationPlace({loader, driver}, false, vkBasalt::lsfgLayerLibrary) == FrameGenerationPlace::NotLoaded,
           "lsfg-vk neither below nor loaded is not loaded");
    expect(vkBasalt::frameGenerationPlace({}, false, vkBasalt::lsfgLayerLibrary) == FrameGenerationPlace::NotLoaded, "an empty chain has no lsfg-vk");
    expect(!vkBasalt::isLayerLibrary("/opt/notlsfg/liblsfg-vk-layer.so.backup/libother.so", vkBasalt::lsfgLayerLibrary),
           "only the file name decides, not a folder named like lsfg-vk's library");
    expect(vkBasalt::isLayerLibrary("liblsfg-vk-layer.so", vkBasalt::lsfgLayerLibrary), "a bare file name is read");

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

    expect(!vkBasalt::orderMismatchWords(FrameGenerationPlace::BelowTheLayer, false, "mako"), "MAKO below a profile asking effects before is no mismatch");
    expect(!vkBasalt::orderMismatchWords(FrameGenerationPlace::AboveTheLayer, true, "mako"), "MAKO above a profile asking effects after is no mismatch");
    expect(!vkBasalt::orderMismatchWords(FrameGenerationPlace::NotLoaded, true, "lsfg-vk"), "no frame generation layer, no mismatch");
    const std::optional<std::string> lost = vkBasalt::orderMismatchWords(FrameGenerationPlace::AboveTheLayer, false, "mako");
    expect(lost && lost->find("asks for effects before") != std::string::npos && lost->find("mako runs above the layer") != std::string::npos
               && lost->find("the effects run after it") != std::string::npos,
           "MAKO above a profile asking effects before names the order asked for and the one that runs: " + lost.value_or("no words"));
    const std::optional<std::string> flipped = vkBasalt::orderMismatchWords(FrameGenerationPlace::BelowTheLayer, true, "lsfg-vk");
    expect(flipped && flipped->find("asks for effects after") != std::string::npos && flipped->find("lsfg-vk runs below the layer") != std::string::npos,
           "lsfg-vk below a profile asking effects after is a mismatch too: " + flipped.value_or("no words"));

    return failures == 0 ? 0 : 1;
}
