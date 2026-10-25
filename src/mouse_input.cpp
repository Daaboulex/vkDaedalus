#include "mouse_input.hpp"
#include "game_input_hook.hpp"

#include "wayland_display.hpp"
#include "mouse_input_wayland.hpp"

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <cstdlib>

namespace vkBasalt
{
    // keyboard and mouse each own an X11 connection, since XNextEvent on a shared one drains the other's events
    static Display* display = nullptr;
    static int xiOpcode = 0;
    static float scrollAccumulator = 0.0f;
    static bool  pressedSinceSample[3] = {};

    static uint64_t pointerQueries    = 0;
    static bool     rawAvailable      = false;
    static bool     rawSelected       = false;
    static bool     overlayReadsMouse = false;

    static bool connectX11()
    {
        if (!display)
        {
            const char* disVar = getenv("DISPLAY");
            if (!disVar || !*disVar)
                return false;

            display = XOpenDisplay(disVar);
            if (!display)
                return false;
            markOverlayX11Display(display);

            int event, error;
            if (XQueryExtension(display, "XInputExtension", &xiOpcode, &event, &error))
            {
                int major = 2, minorWithRawEventsDuringGrabs = 1;
                if (XIQueryVersion(display, &major, &minorWithRawEventsDuringGrabs) == Success)
                {
                    unsigned char mask[XIMaskLen(XI_RawButtonPress)] = {0};
                    XISetMask(mask, XI_RawButtonPress);

                    XIEventMask eventMask = {XIAllMasterDevices, sizeof(mask), mask};
                    XISelectEvents(display, DefaultRootWindow(display), &eventMask, 1);
                    rawAvailable = true;
                    rawSelected  = true;
                }
            }
        }
        return true;
    }

    static void selectRawButtons(bool on)
    {
        if (display == nullptr || !rawAvailable || on == rawSelected)
            return;
        unsigned char mask[XIMaskLen(XI_RawButtonPress)] = {0};
        if (on)
            XISetMask(mask, XI_RawButtonPress);
        XIEventMask eventMask = {XIAllMasterDevices, sizeof(mask), mask};
        XISelectEvents(display, DefaultRootWindow(display), &eventMask, 1);
        XFlush(display);
        rawSelected = on;
    }

    static void drainX11Events()
    {
        while (XPending(display) > 0)
        {
            XEvent ev;
            XNextEvent(display, &ev);
            if (ev.xcookie.type == GenericEvent && ev.xcookie.extension == xiOpcode &&
                XGetEventData(display, &ev.xcookie))
            {
                if (ev.xcookie.evtype == XI_RawButtonPress)
                {
                    int button = ((XIRawEvent*)ev.xcookie.data)->detail;
                    if (button == 4) scrollAccumulator += 1.0f;
                    else if (button == 5) scrollAccumulator -= 1.0f;
                    else if (button >= 1 && button <= 3) pressedSinceSample[button - 1] = true;
                }
                XFreeEventData(display, &ev.xcookie);
            }
        }
    }

    static MouseState getMouseStateX11()
    {
        MouseState state;
        if (!connectX11())
            return state;
        drainX11Events();
        pointerQueries++;

        Window focused, root, child;
        int revertTo, rootX, rootY;
        unsigned int mask;

        XGetInputFocus(display, &focused, &revertTo);
        if (focused == None || focused == PointerRoot)
            focused = DefaultRootWindow(display);

        if (XQueryPointer(display, focused, &root, &child, &rootX, &rootY, &state.x, &state.y, &mask))
        {
            state.leftButton = mask & Button1Mask;
            state.middleButton = mask & Button2Mask;
            state.rightButton = mask & Button3Mask;
        }
        state.leftButton   = state.leftButton || pressedSinceSample[0];
        state.middleButton = state.middleButton || pressedSinceSample[1];
        state.rightButton  = state.rightButton || pressedSinceSample[2];
        pressedSinceSample[0] = pressedSinceSample[1] = pressedSinceSample[2] = false;

        state.scrollUpDelta = scrollAccumulator;
        scrollAccumulator = 0.0f;
        return state;
    }

    static uint64_t   mouseFrameId    = 0;
    static uint64_t   snapshotFrameId = 0;
    static bool       snapshotTaken   = false;
    static MouseState frameSnapshot;

    void setOverlayReadsMouse(bool reads)
    {
        overlayReadsMouse = reads;
    }

    void beginMouseInputFrame(bool effectsReadMouse)
    {
        if (isWayland() || display == nullptr)
        {
            mouseFrameId++;
            return;
        }
        selectRawButtons(overlayReadsMouse || effectsReadMouse);
        if (!(snapshotTaken && snapshotFrameId == mouseFrameId))
        {
            scrollAccumulator     = 0.0f;
            pressedSinceSample[0] = pressedSinceSample[1] = pressedSinceSample[2] = false;
        }
        mouseFrameId++;
        drainX11Events();
    }

    void discardPendingMouseInput()
    {
        if (isWayland())
            getMouseStateWayland();
        else if (display != nullptr)
        {
            drainX11Events();
            scrollAccumulator     = 0.0f;
            pressedSinceSample[0] = pressedSinceSample[1] = pressedSinceSample[2] = false;
        }
        snapshotTaken = false;
    }

    uint64_t mousePointerQueries()
    {
        return pointerQueries;
    }

    MouseState getMouseState()
    {
        if (snapshotTaken && snapshotFrameId == mouseFrameId)
            return frameSnapshot;

        if (isWayland())
            pointerQueries++;
        frameSnapshot   = isWayland() ? getMouseStateWayland() : getMouseStateX11();
        snapshotFrameId = mouseFrameId;
        snapshotTaken   = true;
        return frameSnapshot;
    }

}
