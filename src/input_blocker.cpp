#include "input_blocker.hpp"
#include "wayland_display.hpp"
#include "game_input_hook.hpp"
#include "logger.hpp"

#include <atomic>

#include "keyboard_input_x11.hpp"
#include <X11/Xlib.h>

namespace vkBasalt
{
    static std::atomic<bool> blocked{false};
    static bool interposeUnavailableWarned = false;

    static bool   grabbed     = false;
    static bool   hookHolds   = false;
    static Window focusAtGrab = None;

    static bool grabInput()
    {
        if (grabbed)
            return true;

        Display* display = (Display*)getKeyboardDisplay();
        if (!display)
        {
            Logger::warn("X11: no display for the overlay grab -- input blocking unavailable");
            return false;
        }

        Window root = DefaultRootWindow(display);

        int focusRevert = 0;
        if (XGetInputFocus(display, &focusAtGrab, &focusRevert) == 0)
            focusAtGrab = None;

        int kbResult = XGrabKeyboard(display, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
        int ptrResult = XGrabPointer(display, root, False,
                                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                     GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

        if (kbResult == GrabSuccess && ptrResult == GrabSuccess)
        {
            grabbed = true;
            Logger::debug("Input grabbed for overlay");
        }
        else
        {
            if (kbResult == GrabSuccess)
                XUngrabKeyboard(display, CurrentTime);
            if (ptrResult == GrabSuccess)
                XUngrabPointer(display, CurrentTime);
            Logger::warn("X11: overlay grab refused (keyboard " + std::to_string(kbResult) + ", pointer " + std::to_string(ptrResult)
                         + ") -- input blocking unavailable, the game still receives input");
        }

        XFlush(display);
        return grabbed;
    }

    static void ungrabInput()
    {
        if (!grabbed)
            return;

        Display* display = (Display*)getKeyboardDisplay();
        if (!display)
            return;

        XUngrabKeyboard(display, CurrentTime);
        XUngrabPointer(display, CurrentTime);
        XFlush(display);

        grabbed = false;
        Logger::debug("Input released from overlay");
    }

    static void releaseBlock()
    {
        if (!blocked.load(std::memory_order_acquire))
            return;

        blocked.store(false, std::memory_order_release);

        if (isWayland())
        {
            Logger::debug("Wayland input blocking: forwarding game events");
            setGameInputBlocked(false);
            return;
        }

        if (hookHolds)
        {
            hookHolds = false;
            setGameInputBlocked(false);
            Logger::debug("X11 input hook: forwarding game events");
            return;
        }
        ungrabInput();
    }

    static void engageBlock()
    {
        if (blocked.load(std::memory_order_acquire))
            return;

        if (isWayland())
        {
            if (!gameInputHooked())
            {
                if (!interposeUnavailableWarned)
                {
                    interposeUnavailableWarned = true;
                    Logger::warn("Wayland: input blocking needs the game started with vkdaedalus-run, whose input hook wraps the game's "
                                 "input from its start, and no game input listener was wrapped here, so blocking stays off and the "
                                 "game keeps its input");
                }
                return;
            }
        }
        else if (gameX11InputHooked())
        {
            Display* display = static_cast<Display*>(getKeyboardDisplay());
            int      revert  = 0;
            if (display == nullptr || XGetInputFocus(display, &focusAtGrab, &revert) == 0)
                focusAtGrab = None;
            hookHolds = true;
            setGameInputBlocked(true);
            Logger::debug("X11 input hook: the game's key, button and motion events are held, with no grab");
        }
        else if (!grabInput())
            return;

        blocked.store(true, std::memory_order_release);

        if (isWayland())
        {
            Logger::debug("Wayland input blocking: suppressing game events");
            setGameInputBlocked(true);
        }
    }

    void setInputBlocked(bool shouldBlock)
    {
        if (shouldBlock)
            engageBlock();
        else
            releaseBlock();
    }

    bool isInputBlocked()
    {
        return blocked.load(std::memory_order_acquire);
    }

    bool inputFocusLost()
    {
        if ((!grabbed && !hookHolds) || focusAtGrab == None)
            return false;

        Display* display = (Display*) getKeyboardDisplay();
        if (!display)
            return false;

        Window current = None;
        int    revert  = 0;
        if (XGetInputFocus(display, &current, &revert) == 0)
            return false;

        return current != focusAtGrab;
    }
}
