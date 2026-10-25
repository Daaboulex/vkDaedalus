#include "overlay/desktop_scale.hpp"

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

static bool near(float a, float b)
{
    return std::fabs(a - b) < 0.001f;
}

int main()
{
    using namespace vkBasalt;
    const WaylandOutputFacts laptop{2880, 1800, 1920, 1200, 2};
    const WaylandOutputFacts monitor{2560, 1440, 2560, 1440, 1};

    {
        ScaleInputs in;
        in.chosen         = 1.25f;
        in.waylandOutputs = {laptop};
        in.xftDpi         = 192.0f;
        const DesktopScale got = resolveDesktopScale(in);
        expect(got.source == ScaleSource::Chosen && near(got.scale, 1.25f), "the user's own size wins over the desktop's");
    }
    for (float bad : {0.0f, -1.0f, 9.0f, NAN, INFINITY})
    {
        ScaleInputs in;
        in.chosen          = bad;
        in.waylandOutputs  = {laptop};
        in.swapchainWidth  = 2880;
        in.swapchainHeight = 1800;
        const DesktopScale got = resolveDesktopScale(in);
        expect(got.source == ScaleSource::WaylandOutput && near(got.scale, 1.5f), "an impossible chosen size is ignored, not used");
    }
    {
        ScaleInputs in;
        in.waylandOutputs  = {monitor, laptop};
        in.swapchainWidth  = 2880;
        in.swapchainHeight = 1800;
        const DesktopScale got = resolveDesktopScale(in);
        expect(got.source == ScaleSource::WaylandOutput && near(got.scale, 1.5f), "the output the game fills gives its fractional scale");
    }
    {
        ScaleInputs in;
        in.waylandOutputs  = {laptop, laptop};
        in.swapchainWidth  = 2880;
        in.swapchainHeight = 1800;
        in.xftDpi          = 120.0f;
        const DesktopScale got = resolveDesktopScale(in);
        expect(got.source == ScaleSource::XftDpi && near(got.scale, 1.25f), "two outputs the game could be on are ambiguous, so no output is guessed");
    }
    {
        ScaleInputs in;
        in.waylandOutputs  = {laptop};
        in.swapchainWidth  = 1280;
        in.swapchainHeight = 720;
        const DesktopScale got = resolveDesktopScale(in);
        expect(got.source == ScaleSource::WaylandOutput && near(got.scale, 1.5f), "a windowed game on the only output takes that output's scale");
    }
    {
        ScaleInputs in;
        in.waylandOutputs  = {{3840, 2160, 0, 0, 2}};
        in.swapchainWidth  = 3840;
        in.swapchainHeight = 2160;
        const DesktopScale got = resolveDesktopScale(in);
        expect(got.source == ScaleSource::WaylandOutput && near(got.scale, 2.0f), "without a logical size the output's integer scale is used");
    }
    {
        ScaleInputs in;
        in.waylandOutputs  = {monitor, laptop};
        in.swapchainWidth  = 1280;
        in.swapchainHeight = 720;
        in.xftDpi          = 144.0f;
        const DesktopScale got = resolveDesktopScale(in);
        expect(got.source == ScaleSource::XftDpi && near(got.scale, 1.5f), "with no output matching, X11's Xft.dpi gives the scale");
    }
    {
        ScaleInputs in;
        in.xftDpi = 0.0f;
        const DesktopScale got = resolveDesktopScale(in);
        expect(got.source == ScaleSource::Default && near(got.scale, 1.0f), "nothing usable leaves the scale at 1.0 and says so");
    }

    {
        const std::optional<float> dpi = xftDpiFrom("Xcursor.size:\t24\nXft.dpi:\t144\nXft.antialias:\t1\n");
        expect(dpi && near(*dpi, 144.0f), "Xft.dpi is read from among the other X resources");
        expect(!xftDpiFrom("Xft.dpi:\tlarge\n"), "an Xft.dpi that is not a number gives no dpi");
        expect(!xftDpiFrom("Xft.dpi:\t-96\n"), "a negative Xft.dpi gives no dpi");
        expect(!xftDpiFrom("Xft.dpiX:\t192\n"), "a resource whose name only starts with Xft.dpi is not it");
        expect(!xftDpiFrom(""), "no resources give no dpi");
    }
    if (failures == 0)
        std::printf("desktop scale: every case holds\n");
    {
        ScaleInputs in;
        in.waylandOutputs  = {WaylandOutputFacts{2880, 1800, 1920, 1200, 2, 120000}, WaylandOutputFacts{2560, 1440, 2560, 1440, 1, 59951}};
        in.swapchainWidth  = 2560;
        in.swapchainHeight = 1440;
        expect(gameOutputRefreshHz(in) && near(static_cast<float>(*gameOutputRefreshHz(in)), 59.951f), "the refresh is the current mode's of the output the swapchain fills");
        in.waylandOutputs[1].refreshMilliHz = 0;
        expect(!gameOutputRefreshHz(in), "an output that reports no refresh gives none, never a guess");
        in.swapchainWidth = 1280;
        expect(!gameOutputRefreshHz(in), "with two outputs and none the swapchain's size, no refresh is named");
    }
    return failures == 0 ? 0 : 1;
}
