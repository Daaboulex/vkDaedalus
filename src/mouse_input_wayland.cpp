#include "mouse_input_wayland.hpp"
#include "wayland_input_common.hpp"
#include "game_input_hook.hpp"
#include "wayland_pointer_state.hpp"
#include "logger.hpp"

#include <wayland-client.h>
#include "relative-pointer-unstable-v1-client-protocol.h"

#include <mutex>

namespace vkBasalt
{
    static wl_pointer*              wlPointer       = nullptr;
    static zwp_relative_pointer_v1* relativePointer = nullptr;

    static std::mutex          stateMutex;
    static WaylandPointerState state;

    static bool bindCallbackSet = false;
    static bool mouseLogged     = false;
    static bool noPointerWarned = false;

    static void pointerEnter(void*, wl_pointer*,
                             uint32_t, wl_surface*,
                             wl_fixed_t sx, wl_fixed_t sy)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.absoluteMotion(wl_fixed_to_double(sx), wl_fixed_to_double(sy));

        if (Logger::logLevel() <= LogLevel::Trace)
            Logger::trace("Wayland: pointer enter at " + std::to_string(wl_fixed_to_int(sx)) + "," + std::to_string(wl_fixed_to_int(sy)));
    }

    static void pointerLeave(void*, wl_pointer*,
                             uint32_t, wl_surface*)
    {
        if (Logger::logLevel() <= LogLevel::Trace)
            Logger::trace("Wayland: pointer leave");
    }

    static void pointerMotion(void*, wl_pointer*,
                              uint32_t, wl_fixed_t sx, wl_fixed_t sy)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.absoluteMotion(wl_fixed_to_double(sx), wl_fixed_to_double(sy));
    }

#ifdef WL_POINTER_WARP_SINCE_VERSION
    static void pointerWarp(void*, wl_pointer*,
                            wl_fixed_t sx, wl_fixed_t sy)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.absoluteMotion(wl_fixed_to_double(sx), wl_fixed_to_double(sy));
    }
#endif

    static void pointerButton(void*, wl_pointer*,
                              uint32_t, uint32_t,
                              uint32_t button, uint32_t buttonState)
    {
        const bool pressed = (buttonState == WL_POINTER_BUTTON_STATE_PRESSED);
        if (Logger::logLevel() <= LogLevel::Trace)
            Logger::trace("Wayland: pointer button " + std::to_string(button) + " " + (pressed ? "pressed" : "released"));

        std::lock_guard<std::mutex> lock(stateMutex);
        state.button(button, pressed, WaylandPointerState::Clock::now());
    }

    static void pointerAxis(void*, wl_pointer*,
                            uint32_t, uint32_t axis, wl_fixed_t value)
    {
        if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
            return;
        std::lock_guard<std::mutex> lock(stateMutex);
        state.scrollContinuous(wl_fixed_to_double(value));
    }

    static void pointerFrame(void*, wl_pointer*)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.frame();
    }

    static void pointerAxisSource(void*, wl_pointer*, uint32_t)
    {
    }

    static void pointerAxisStop(void*, wl_pointer*,
                                uint32_t, uint32_t)
    {
    }

    static void pointerAxisDiscrete(void*, wl_pointer*,
                                    uint32_t axis, int32_t discrete)
    {
        if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
            return;
        std::lock_guard<std::mutex> lock(stateMutex);
        state.scrollDiscrete(discrete);
    }

#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
    static void pointerAxisValue120(void*, wl_pointer*,
                                    uint32_t axis, int32_t value120)
    {
        if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
            return;
        std::lock_guard<std::mutex> lock(stateMutex);
        state.scrollDiscrete(value120 / 120.0);
    }
#endif

#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
    static void pointerAxisRelativeDirection(void*, wl_pointer*,
                                             uint32_t, uint32_t)
    {
    }
#endif

    static const wl_pointer_listener pointerListener = {
        .enter = pointerEnter,
        .leave = pointerLeave,
        .motion = pointerMotion,
        .button = pointerButton,
        .axis = pointerAxis,
        .frame = pointerFrame,
        .axis_source = pointerAxisSource,
        .axis_stop = pointerAxisStop,
        .axis_discrete = pointerAxisDiscrete,
#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
        .axis_value120 = pointerAxisValue120,
#endif
#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
        .axis_relative_direction = pointerAxisRelativeDirection,
#endif
#ifdef WL_POINTER_WARP_SINCE_VERSION
        .warp = pointerWarp,
#endif
    };

    static void relativeMotion(void*, zwp_relative_pointer_v1*,
                               uint32_t, uint32_t,
                               wl_fixed_t dx, wl_fixed_t dy,
                               wl_fixed_t, wl_fixed_t)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.relativeMotion(wl_fixed_to_double(dx), wl_fixed_to_double(dy));
    }

    static const zwp_relative_pointer_v1_listener relativePointerListener = {
        .relative_motion = relativeMotion,
    };

    static void bindPointer(wl_seat* seat)
    {
        if (wlPointer)
            return;

        wlPointer = wl_seat_get_pointer(seat);
        addOverlayListener((wl_proxy*)wlPointer, &pointerListener, nullptr);

        zwp_relative_pointer_manager_v1* manager = getWaylandRelativePointerManager();
        if (!manager)
        {
            Logger::warn("Wayland: the compositor offers no zwp_relative_pointer_manager_v1, so the overlay cursor "
                         "cannot move while a game holds the pointer locked");
            return;
        }

        relativePointer = zwp_relative_pointer_manager_v1_get_relative_pointer(manager, wlPointer);
        zwp_relative_pointer_v1_add_listener(relativePointer, &relativePointerListener, nullptr);
        Logger::debug("Wayland: pointer and relative pointer bound from shared seat");
    }

    bool initWaylandMouse()
    {
        if (!bindCallbackSet)
        {
            setPointerBindCallback(bindPointer);
            bindCallbackSet = true;
        }

        if (!initWaylandInputCommon())
            return false;

        if (wlPointer && !mouseLogged)
        {
            mouseLogged = true;
            Logger::info("Wayland mouse input initialized");
        }
        else if (!wlPointer && !noPointerWarned)
        {
            noPointerWarned = true;
            Logger::warn("Wayland: no pointer found on seat");
        }

        return wlPointer != nullptr;
    }

    void cleanupWaylandMouse()
    {
        if (relativePointer)
        {
            zwp_relative_pointer_v1_destroy(relativePointer);
            relativePointer = nullptr;
        }
        if (wlPointer)
        {
            unregisterOverlayProxy((wl_proxy*)wlPointer);
            wl_pointer_destroy(wlPointer);
            wlPointer = nullptr;
        }

        bindCallbackSet = false;
        mouseLogged     = false;
        noPointerWarned = false;
    }

    void setWaylandPointerBounds(int width, int height)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.setBounds(width, height);
    }

    void mirrorButtonState(uint32_t button, bool pressed)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        state.button(button, pressed, WaylandPointerState::Clock::now());
    }

    MouseState getMouseStateWayland()
    {
        MouseState result;

        const bool ready = initWaylandMouse();
        dispatchWaylandInputEvents();
        takeGameButtons(mirrorButtonState);
        if (!ready)
            return result;

        std::lock_guard<std::mutex> lock(stateMutex);
        const PointerSample sample = state.sample(WaylandPointerState::Clock::now());

        result.x            = sample.x;
        result.y            = sample.y;
        result.leftButton   = sample.left;
        result.rightButton  = sample.right;
        result.middleButton = sample.middle;
        result.scrollUpDelta  = sample.scroll;
        return result;
    }
}
