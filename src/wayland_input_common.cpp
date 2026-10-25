#include "wayland_input_common.hpp"
#include "wayland_display.hpp"
#include "logger.hpp"

#include "relative-pointer-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#include <cstring>
#include <list>
#include <poll.h>

namespace vkBasalt
{
    static wl_display* displayWrapper = nullptr;
    static wl_event_queue* queue = nullptr;
    static wl_registry* registry = nullptr;
    static wl_seat* seat = nullptr;
    static zwp_relative_pointer_manager_v1* relativePointerManager = nullptr;
    struct OutputRecord
    {
        uint32_t           name   = 0;
        wl_output*         output = nullptr;
        zxdg_output_v1*    xdg    = nullptr;
        WaylandOutputFacts facts;
    };
    static std::list<OutputRecord>  outputs;
    static zxdg_output_manager_v1* xdgOutputManager = nullptr;
    static bool commonInitialized = false;
    static bool noDisplayWarned = false;
    static unsigned handshakeAttempts = 0;
    static constexpr unsigned maxHandshakeAttempts = 10;

    static uint64_t dispatchFrameId = 0;
    static uint64_t lastDispatchedFrame = 0;

    static KeyboardBindCallback keyboardBind = nullptr;
    static PointerBindCallback pointerBind = nullptr;

    void setKeyboardBindCallback(KeyboardBindCallback cb)
    {
        keyboardBind = cb;
        if (seat && cb)
            cb(seat);
    }

    void setPointerBindCallback(PointerBindCallback cb)
    {
        pointerBind = cb;
        if (seat && cb)
            cb(seat);
    }

    static void seatCapabilities(void*, wl_seat* s, uint32_t caps)
    {
        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && keyboardBind)
            keyboardBind(s);
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && pointerBind)
            pointerBind(s);
    }

    static void seatName(void*, wl_seat*, const char*)
    {
    }

    static const wl_seat_listener sharedSeatListener = {
        .capabilities = seatCapabilities,
        .name = seatName,
    };

    static void outputGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}

    static void outputMode(void* data, wl_output*, uint32_t flags, int32_t width, int32_t height, int32_t refresh)
    {
        if (!(flags & WL_OUTPUT_MODE_CURRENT))
            return;
        OutputRecord* record          = static_cast<OutputRecord*>(data);
        record->facts.modeWidth       = width;
        record->facts.modeHeight      = height;
        record->facts.refreshMilliHz  = refresh;
    }

    static void outputDone(void*, wl_output*) {}

    static void outputScale(void* data, wl_output*, int32_t factor)
    {
        static_cast<OutputRecord*>(data)->facts.integerScale = factor;
    }

    static const wl_output_listener outputListener = {
        .geometry = outputGeometry,
        .mode     = outputMode,
        .done     = outputDone,
        .scale    = outputScale,
    };

    static void xdgOutputPosition(void*, zxdg_output_v1*, int32_t, int32_t) {}

    static void xdgOutputSize(void* data, zxdg_output_v1*, int32_t width, int32_t height)
    {
        OutputRecord* record        = static_cast<OutputRecord*>(data);
        record->facts.logicalWidth  = width;
        record->facts.logicalHeight = height;
    }

    static void xdgOutputDone(void*, zxdg_output_v1*) {}

    static const zxdg_output_v1_listener xdgOutputListener = {
        .logical_position = xdgOutputPosition,
        .logical_size     = xdgOutputSize,
        .done             = xdgOutputDone,
    };

    static void watchLogicalSize(OutputRecord& record)
    {
        if (!xdgOutputManager || record.xdg)
            return;
        record.xdg = zxdg_output_manager_v1_get_xdg_output(xdgOutputManager, record.output);
        zxdg_output_v1_add_listener(record.xdg, &xdgOutputListener, &record);
    }

    static void registryGlobal(void*, wl_registry* reg,
                               uint32_t name, const char* interface, uint32_t version)
    {
        if (std::strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0 && !relativePointerManager)
        {
            relativePointerManager = (zwp_relative_pointer_manager_v1*)wl_registry_bind(
                reg, name, &zwp_relative_pointer_manager_v1_interface, 1);
            Logger::debug("Wayland: relative pointer manager bound");
            return;
        }

        if (std::strcmp(interface, wl_output_interface.name) == 0)
        {
            OutputRecord& record = outputs.emplace_back();
            record.name          = name;
            record.output        = (wl_output*)wl_registry_bind(reg, name, &wl_output_interface, version < 2 ? version : 2);
            wl_output_add_listener(record.output, &outputListener, &record);
            watchLogicalSize(record);
            return;
        }

        if (std::strcmp(interface, zxdg_output_manager_v1_interface.name) == 0 && !xdgOutputManager)
        {
            xdgOutputManager = (zxdg_output_manager_v1*)wl_registry_bind(reg, name, &zxdg_output_manager_v1_interface, 1);
            for (OutputRecord& record : outputs)
                watchLogicalSize(record);
            return;
        }

        if (std::strcmp(interface, wl_seat_interface.name) != 0)
            return;
        if (seat)
            return;

        seat = (wl_seat*)wl_registry_bind(reg, name, &wl_seat_interface,
                                            version < 5 ? version : 5);
        wl_seat_add_listener(seat, &sharedSeatListener, nullptr);
        Logger::info("Wayland: seat bound");
    }

    static void forgetOutput(OutputRecord& record)
    {
        if (record.xdg)
            zxdg_output_v1_destroy(record.xdg);
        if (record.output)
            wl_output_destroy(record.output);
    }

    static void registryGlobalRemove(void*, wl_registry*, uint32_t name)
    {
        for (auto at = outputs.begin(); at != outputs.end(); ++at)
            if (at->name == name)
            {
                forgetOutput(*at);
                outputs.erase(at);
                return;
            }
    }

    static const wl_registry_listener registryListener = {
        .global = registryGlobal,
        .global_remove = registryGlobalRemove,
    };

    wl_event_queue* getWaylandInputQueue()
    {
        return queue;
    }

    wl_seat* getWaylandSeat()
    {
        return seat;
    }

    zwp_relative_pointer_manager_v1* getWaylandRelativePointerManager()
    {
        return relativePointerManager;
    }

    std::vector<WaylandOutputFacts> getWaylandOutputs()
    {
        std::vector<WaylandOutputFacts> facts;
        for (const OutputRecord& record : outputs)
            facts.push_back(record.facts);
        return facts;
    }

    bool initWaylandInputCommon()
    {
        if (commonInitialized)
            return seat != nullptr;

        wl_display* display = getWaylandDisplay();
        if (!display)
        {
            if (!noDisplayWarned)
            {
                noDisplayWarned = true;
                Logger::warn("Wayland: no display captured in this process -- overlay input unavailable. The layer can only "
                             "borrow the display the application passes to vkCreateWaylandSurfaceKHR; under wine this needs "
                             "the LD_AUDIT shim that vkdaedalus-run sets.");
            }
            return false;
        }

        displayWrapper = (wl_display*)wl_proxy_create_wrapper(display);
        if (!displayWrapper)
            return false;

        queue = wl_display_create_queue(display);
        if (!queue)
        {
            wl_proxy_wrapper_destroy(displayWrapper);
            displayWrapper = nullptr;
            return false;
        }

        wl_proxy_set_queue((wl_proxy*)displayWrapper, queue);

        registry = wl_display_get_registry(displayWrapper);
        wl_registry_add_listener(registry, &registryListener, nullptr);

        const bool globalsArrived = wl_display_roundtrip_queue(display, queue) >= 0;
        const bool seatCapabilitiesArrived = globalsArrived && wl_display_roundtrip_queue(display, queue) >= 0;

        if (!seatCapabilitiesArrived)
        {
            // a registry announces existing globals once, at creation, so a failed handshake needs a new registry
            handshakeAttempts++;
            Logger::warn("Wayland: registry roundtrip failed (display error " + std::to_string(wl_display_get_error(display))
                         + "), attempt " + std::to_string(handshakeAttempts) + " of " + std::to_string(maxHandshakeAttempts));

            wl_registry_destroy(registry);
            registry = nullptr;
            wl_event_queue_destroy(queue);
            queue = nullptr;
            wl_proxy_wrapper_destroy(displayWrapper);
            displayWrapper = nullptr;

            if (handshakeAttempts >= maxHandshakeAttempts)
            {
                commonInitialized = true;
                Logger::err("Wayland: registry handshake failed " + std::to_string(maxHandshakeAttempts)
                            + " times -- giving up, overlay input stays unavailable in this process");
            }
            return false;
        }

        commonInitialized = true;

        if (seat)
            Logger::info("Wayland: shared input resources initialized");
        else
            Logger::warn("Wayland: no seat found, binding it if it appears");

        return seat != nullptr;
    }

    void beginWaylandInputFrame()
    {
        dispatchFrameId++;
    }

    void dispatchWaylandInputEvents()
    {
        if (lastDispatchedFrame == dispatchFrameId)
            return;
        lastDispatchedFrame = dispatchFrameId;

        if (!queue)
            return;

        wl_display* display = getWaylandDisplay();
        if (!display)
            return;

        while (wl_display_prepare_read_queue(display, queue) != 0)
            wl_display_dispatch_queue_pending(display, queue);

        // many games only call wl_display_dispatch_pending, which never reads the socket
        wl_display_flush(display);
        struct pollfd pfd = { wl_display_get_fd(display), POLLIN, 0 };
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
            wl_display_read_events(display);
        else
            wl_display_cancel_read(display);

        wl_display_dispatch_queue_pending(display, queue);
    }

    void cleanupWaylandInputCommon()
    {
        for (OutputRecord& record : outputs)
            forgetOutput(record);
        outputs.clear();
        if (xdgOutputManager)
        {
            zxdg_output_manager_v1_destroy(xdgOutputManager);
            xdgOutputManager = nullptr;
        }
        if (relativePointerManager)
        {
            zwp_relative_pointer_manager_v1_destroy(relativePointerManager);
            relativePointerManager = nullptr;
        }
        if (seat)
        {
            wl_seat_destroy(seat);
            seat = nullptr;
        }
        if (registry)
        {
            wl_registry_destroy(registry);
            registry = nullptr;
        }
        // libwayland warns when a queue is destroyed while a proxy is still attached to it
        if (displayWrapper)
        {
            wl_proxy_wrapper_destroy(displayWrapper);
            displayWrapper = nullptr;
        }
        if (queue)
        {
            wl_event_queue_destroy(queue);
            queue = nullptr;
        }

        keyboardBind = nullptr;
        pointerBind = nullptr;
        commonInitialized = false;
        noDisplayWarned = false;
        handshakeAttempts = 0;
    }

}
