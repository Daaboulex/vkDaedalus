#pragma once

#include <wayland-client.h>

#include <vector>

#include "overlay/desktop_scale.hpp"

struct zwp_relative_pointer_manager_v1;

namespace vkBasalt
{
    // a wl_seat takes a single listener, so the keyboard and mouse backends bind through these callbacks

    using KeyboardBindCallback = void (*)(wl_seat* seat);
    using PointerBindCallback = void (*)(wl_seat* seat);

    void setKeyboardBindCallback(KeyboardBindCallback cb);
    void setPointerBindCallback(PointerBindCallback cb);

    bool initWaylandInputCommon();

    void cleanupWaylandInputCommon();

    wl_event_queue* getWaylandInputQueue();

    wl_seat* getWaylandSeat();

    zwp_relative_pointer_manager_v1* getWaylandRelativePointerManager();

    void beginWaylandInputFrame();

    void dispatchWaylandInputEvents();

    std::vector<WaylandOutputFacts> getWaylandOutputs();

}
