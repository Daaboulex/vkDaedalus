#pragma once

#include <cstdint>
#include <string>
#include "keyboard_input.hpp"

namespace vkBasalt
{
    bool     isKeyPressedWayland(uint32_t ks);
    KeyboardState getKeyboardStateWayland();

    bool initWaylandKeyboard();

    void cleanupWaylandKeyboard();
}
