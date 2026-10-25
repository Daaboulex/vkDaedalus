#pragma once

#include <cstdint>
#include <string>
#include "keyboard_input.hpp"

namespace vkBasalt
{
    bool     isKeyPressedX11(uint32_t ks);
    KeyboardState getKeyboardStateX11();

    void* getKeyboardDisplay();

    std::string x11ResourceString();
    void        setX11TypingEvents(bool on);
}
