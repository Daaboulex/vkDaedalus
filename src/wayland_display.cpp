#include "wayland_display.hpp"

#include "logger.hpp"

#include <cstdlib>

namespace vkBasalt
{
    static wl_display* waylandDisplay = nullptr;
    static wl_surface* waylandSurface = nullptr;
    enum class InputBackend
    {
        Undetermined,
        X11,
        Wayland
    };
    static InputBackend inputBackend = InputBackend::Undetermined;

    void setWaylandDisplay(wl_display* display)
    {
        if (!display)
            return;

        waylandDisplay = display;
        inputBackend = InputBackend::Wayland;
        Logger::info("captured Wayland display from vkCreateWaylandSurfaceKHR");
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

    void useX11InputBackend()
    {
        if (inputBackend == InputBackend::Wayland)
            return;

        inputBackend = InputBackend::X11;
        Logger::info("detected X11 (Xlib/Xcb) client -> X11 input backend");
    }

    bool isWayland()
    {
        if (inputBackend != InputBackend::Undetermined)
            return inputBackend == InputBackend::Wayland;

        const char* wlDisplay = getenv("WAYLAND_DISPLAY");
        return wlDisplay && *wlDisplay;
    }
}
