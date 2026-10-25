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
    useX11InputBackend();
    expect(!isWayland(), "an X11 surface decides X11 even though WAYLAND_DISPLAY was set and already asked about");

    setWaylandDisplay(reinterpret_cast<wl_display*>(&failures));
    expect(isWayland(), "a captured Wayland display decides Wayland");
    useX11InputBackend();
    expect(isWayland(), "a later X11 surface does not take the Wayland backend away again");

    if (failures != 0)
        return 1;
    std::printf("OK: the input backend follows the surfaces, the environment only guesses\n");
    return 0;
}
