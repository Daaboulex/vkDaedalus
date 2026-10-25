#include "keyboard_input.hpp"

#include "logger.hpp"
#include "wayland_display.hpp"

#include "keyboard_input_x11.hpp"

#include "keyboard_input_wayland.hpp"

#include <xkbcommon/xkbcommon.h>

namespace vkBasalt
{
    uint32_t convertToKeySym(std::string key)
    {
        xkb_keysym_t sym = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_NO_FLAGS);
        if (sym == XKB_KEY_NoSymbol)
            sym = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
        if (sym == XKB_KEY_NoSymbol)
            Logger::err("invalid key name: " + key);
        return static_cast<uint32_t>(sym);
    }

    bool isKeyPressed(uint32_t ks)
    {
        if (isWayland())
            return isKeyPressedWayland(ks);
        return isKeyPressedX11(ks);
    }

    void setOverlayTyping(bool on)
    {
        if (!isWayland())
            setX11TypingEvents(on);
    }

    KeyboardState getKeyboardState()
    {
        KeyboardState state = isWayland() ? getKeyboardStateWayland() : getKeyboardStateX11();
        state.ctrl          = isKeyPressed(XKB_KEY_Control_L) || isKeyPressed(XKB_KEY_Control_R);
        state.shift         = isKeyPressed(XKB_KEY_Shift_L) || isKeyPressed(XKB_KEY_Shift_R);
        state.alt           = isKeyPressed(XKB_KEY_Alt_L) || isKeyPressed(XKB_KEY_Alt_R);
        return state;
    }
}
