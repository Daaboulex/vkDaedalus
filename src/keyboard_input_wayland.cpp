#include "keyboard_input_wayland.hpp"
#include "wayland_input_common.hpp"
#include "game_input_hook.hpp"
#include "wayland_display.hpp"
#include "logger.hpp"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/mman.h>
#include <unordered_set>

namespace vkBasalt
{
    static wl_keyboard* wlKeyboard = nullptr;

    static xkb_context* xkbCtx = nullptr;
    static xkb_keymap* xkbKeymap = nullptr;
    static xkb_state* xkbState = nullptr;

    static std::unordered_set<uint32_t> pressedKeys;

    static std::unordered_set<uint32_t> pressesSincePoll;

    static constexpr size_t   TYPED_CHARS_LIMIT           = 256;
    static constexpr uint32_t EVDEV_TO_XKB_KEYCODE_OFFSET = 8;
    static std::string typedCharsAccumulator;
    static std::string lastKeyNameAccumulator;
    static bool backspacePressed = false;
    static bool deletePressed = false;
    static bool enterPressed = false;
    static bool leftPressed = false;
    static bool rightPressed = false;
    static bool homePressed = false;
    static bool endPressed = false;
    static bool tabPressed = false;

    static bool keyboardLogged = false;
    static bool noKeyboardWarned = false;

    static void processWaylandKey(uint32_t keycode, uint32_t state)
    {
        if (!xkbState)
            return;

        uint32_t xkbKeycode = keycode + EVDEV_TO_XKB_KEYCODE_OFFSET;

        if (state == WL_KEYBOARD_KEY_STATE_RELEASED)
        {
            pressedKeys.erase(keycode);
            xkb_state_update_key(xkbState, xkbKeycode, XKB_KEY_UP);
            return;
        }

        pressedKeys.insert(keycode);
        xkb_state_update_key(xkbState, xkbKeycode, XKB_KEY_DOWN);

        xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkbState, xkbKeycode);

        pressesSincePoll.insert((uint32_t)keysym);

        if (keysym != XKB_KEY_Shift_L && keysym != XKB_KEY_Shift_R &&
            keysym != XKB_KEY_Control_L && keysym != XKB_KEY_Control_R &&
            keysym != XKB_KEY_Alt_L && keysym != XKB_KEY_Alt_R &&
            keysym != XKB_KEY_Super_L && keysym != XKB_KEY_Super_R)
        {
            char nameBuf[64];
            if (xkb_keysym_get_name(keysym, nameBuf, sizeof(nameBuf)) > 0)
                lastKeyNameAccumulator = nameBuf;
        }

        if (keysym == XKB_KEY_BackSpace)
            backspacePressed = true;
        else if (keysym == XKB_KEY_Delete)
            deletePressed = true;
        else if (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter)
            enterPressed = true;
        else if (keysym == XKB_KEY_Left)
            leftPressed = true;
        else if (keysym == XKB_KEY_Right)
            rightPressed = true;
        else if (keysym == XKB_KEY_Home)
            homePressed = true;
        else if (keysym == XKB_KEY_End)
            endPressed = true;
        else if (keysym == XKB_KEY_Tab || keysym == XKB_KEY_ISO_Left_Tab)
            tabPressed = true;
        else
        {
            char buf[8];
            int len = xkb_state_key_get_utf8(xkbState, xkbKeycode, buf, sizeof(buf));
            if (len > 0 && buf[0] >= 0x20 && typedCharsAccumulator.size() + len <= TYPED_CHARS_LIMIT)
                typedCharsAccumulator += std::string(buf, len);
        }
    }

    static void keyboardKeymap(void*, wl_keyboard*,
                               uint32_t format, int32_t fd, uint32_t size)
    {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
        {
            close(fd);
            return;
        }

        char* mapStr = (char*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapStr == MAP_FAILED)
        {
            close(fd);
            Logger::err("Wayland: failed to mmap keymap");
            return;
        }

        if (xkbState)
        {
            xkb_state_unref(xkbState);
            xkbState = nullptr;
        }
        if (xkbKeymap)
        {
            xkb_keymap_unref(xkbKeymap);
            xkbKeymap = nullptr;
        }

        xkbKeymap = xkb_keymap_new_from_string(xkbCtx, mapStr,
                                                 XKB_KEYMAP_FORMAT_TEXT_V1,
                                                 XKB_KEYMAP_COMPILE_NO_FLAGS);
        munmap(mapStr, size);
        close(fd);

        if (!xkbKeymap)
        {
            Logger::err("Wayland: failed to compile keymap");
            return;
        }

        xkbState = xkb_state_new(xkbKeymap);
        if (!xkbState)
            Logger::err("Wayland: failed to create XKB state");

        Logger::debug("Wayland: keymap loaded");
    }

    static void keyboardEnter(void*, wl_keyboard*,
                              uint32_t, wl_surface*,
                              wl_array*)
    {
    }

    static void keyboardLeave(void*, wl_keyboard*,
                              uint32_t, wl_surface*)
    {
        pressedKeys.clear();
        pressesSincePoll.clear();
    }

    static void keyboardKey(void*, wl_keyboard*,
                            uint32_t, uint32_t,
                            uint32_t key, uint32_t state)
    {
        processWaylandKey(key, state);
    }

    static void keyboardModifiers(void*, wl_keyboard*,
                                  uint32_t, uint32_t modsDepressed,
                                  uint32_t modsLatched, uint32_t modsLocked,
                                  uint32_t group)
    {
        if (xkbState)
            xkb_state_update_mask(xkbState, modsDepressed, modsLatched, modsLocked, 0, 0, group);
    }

    static void keyboardRepeatInfo(void*, wl_keyboard*,
                                   int32_t, int32_t)
    {
    }

    static const wl_keyboard_listener keyboardListener = {
        .keymap = keyboardKeymap,
        .enter = keyboardEnter,
        .leave = keyboardLeave,
        .key = keyboardKey,
        .modifiers = keyboardModifiers,
        .repeat_info = keyboardRepeatInfo,
    };

    static void bindKeyboard(wl_seat* seat)
    {
        if (wlKeyboard)
            return;

        wlKeyboard = wl_seat_get_keyboard(seat);
        addOverlayListener((wl_proxy*)wlKeyboard, &keyboardListener, nullptr);
        Logger::debug("Wayland: keyboard bound from shared seat");
    }

    bool initWaylandKeyboard()
    {
        if (!xkbCtx)
        {
            xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            if (!xkbCtx)
            {
                Logger::err("Wayland: failed to create XKB context");
                return false;
            }
            setKeyboardBindCallback(bindKeyboard);
        }

        if (!initWaylandInputCommon())
            return false;

        if (wlKeyboard && !keyboardLogged)
        {
            keyboardLogged = true;
            Logger::info("Wayland keyboard input initialized");
        }
        else if (!wlKeyboard && !noKeyboardWarned)
        {
            noKeyboardWarned = true;
            Logger::warn("Wayland: no keyboard found on seat");
        }

        return wlKeyboard != nullptr;
    }

    void cleanupWaylandKeyboard()
    {
        if (wlKeyboard)
        {
            unregisterOverlayProxy((wl_proxy*)wlKeyboard);
            wl_keyboard_destroy(wlKeyboard);
            wlKeyboard = nullptr;
        }
        if (xkbState)
        {
            xkb_state_unref(xkbState);
            xkbState = nullptr;
        }
        if (xkbKeymap)
        {
            xkb_keymap_unref(xkbKeymap);
            xkbKeymap = nullptr;
        }
        if (xkbCtx)
        {
            xkb_context_unref(xkbCtx);
            xkbCtx = nullptr;
        }

        pressedKeys.clear();
        pressesSincePoll.clear();
        keyboardLogged = false;
        noKeyboardWarned = false;
    }

    bool isKeyPressedWayland(uint32_t ks)
    {
        bool ready = initWaylandKeyboard();

        // dispatching while unbound is what delivers a late wl_seat
        dispatchWaylandInputEvents();

        if (!ready)
            return false;

        if (pressesSincePoll.count(ks))
        {
            pressesSincePoll.erase(ks);
            return true;
        }

        if (!xkbKeymap)
            return false;

        for (uint32_t keycode : pressedKeys)
        {
            uint32_t xkbKeycode = keycode + EVDEV_TO_XKB_KEYCODE_OFFSET;
            xkb_keysym_t sym = xkb_state_key_get_one_sym(xkbState, xkbKeycode);
            if ((uint32_t)sym == ks)
                return true;
        }
        return false;
    }

    KeyboardState getKeyboardStateWayland()
    {
        KeyboardState state;

        bool ready = initWaylandKeyboard();
        dispatchWaylandInputEvents();
        if (!ready)
            return state;

        state.typedChars = std::move(typedCharsAccumulator);
        state.lastKeyName = std::move(lastKeyNameAccumulator);
        state.backspace = backspacePressed;
        state.del = deletePressed;
        state.enter = enterPressed;
        state.left = leftPressed;
        state.right = rightPressed;
        state.home = homePressed;
        state.end = endPressed;
        state.tab = tabPressed;

        typedCharsAccumulator.clear();
        lastKeyNameAccumulator.clear();
        backspacePressed = false;
        deletePressed = false;
        enterPressed = false;
        leftPressed = false;
        rightPressed = false;
        homePressed = false;
        endPressed = false;
        tabPressed = false;

        return state;
    }
}
