#include "wayland_display.hpp"

#include <cstdio>
#include <cstdlib>

using namespace vkBasalt;

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
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    expect(isWayland(), "with nothing decided yet the environment is the guess");
    noteX11Instance();
    expect(!isWayland(), "an instance that enables only X11 surfaces decides X11 although WAYLAND_DISPLAY was set and already asked about");

    setWaylandDisplay(reinterpret_cast<wl_display*>(&failures));
    expect(isWayland(), "a Wayland surface decides Wayland, whatever an instance enabling only X11 surfaces said");
    noteX11Surface();
    expect(!isWayland(), "an X11 surface decides X11 beside a Wayland surface: gamescope's WSI layer presents the game's X11 window "
                         "through a Wayland connection of its own, and the game's input stays with its X11 window");
    setWaylandDisplay(reinterpret_cast<wl_display*>(&failures));
    expect(!isWayland(), "a later Wayland surface, as the WSI layer makes for every new swapchain surface, leaves input with X11");

    if (failures != 0)
        return 1;
    std::printf("OK: the input backend follows the game's window: an X11 surface, else a Wayland one, else the instance, else the environment\n");
    return 0;
}
