#include "wayland_display.hpp"

#include "logger.hpp"

#include <cstdlib>

namespace vkBasalt
{
    static wl_display* waylandDisplay = nullptr;
    static wl_surface* waylandSurface = nullptr;
    static bool        x11Surface     = false;
    static bool        x11Instance    = false;

    void setWaylandDisplay(wl_display* display)
    {
        if (!display)
            return;

        waylandDisplay = display;
        Logger::info(x11Surface ? "captured Wayland display from vkCreateWaylandSurfaceKHR beside the game's X11 window, so input stays with X11"
                                : "captured Wayland display from vkCreateWaylandSurfaceKHR");
    }

    wl_display* getWaylandDisplay()
    {
        return waylandDisplay;
    }

    void setWaylandSurface(wl_surface* surface)
    {
        if (!surface)
            return;

        waylandSurface = surface;
        Logger::info("captured Wayland surface from vkCreateWaylandSurfaceKHR");
    }

    wl_surface* getWaylandSurface()
    {
        return waylandSurface;
    }

    void noteX11Instance()
    {
        if (x11Instance)
            return;

        x11Instance = true;
        Logger::info("an instance enables only X11 surfaces -> X11 input backend until a surface says otherwise");
    }

    void noteX11Surface()
    {
        if (x11Surface)
            return;

        x11Surface = true;
        Logger::info(waylandDisplay ? "the game's window is X11 (Xlib/Xcb surface) and a layer above presents it through a Wayland connection of its own, "
                                      "as gamescope's WSI layer does, so input follows X11"
                                    : "detected X11 (Xlib/Xcb) client -> X11 input backend");
    }

    bool isWayland()
    {
        if (x11Surface)
            return false;
        if (waylandDisplay)
            return true;
        if (x11Instance)
            return false;

        const char* wlDisplay = getenv("WAYLAND_DISPLAY");
        return wlDisplay && *wlDisplay;
    }
}
