#pragma once

struct wl_display;
struct wl_surface;

namespace vkBasalt
{
    void setWaylandDisplay(wl_display* display);

    wl_display* getWaylandDisplay();

    void setWaylandSurface(wl_surface* surface);

    wl_surface* getWaylandSurface();

    void noteX11Instance();

    void noteX11Surface();

    bool isWayland();
}
