#pragma once

#include <cstdint>
#include <string>

namespace vkBasalt
{
    struct KeyboardState
    {
        std::string typedChars;
        std::string lastKeyName;
        bool backspace = false;
        bool del = false;
        bool enter = false;
        bool left = false;
        bool right = false;
        bool home = false;
        bool end = false;
        bool tab = false;
        bool ctrl = false;
        bool shift = false;
        bool alt = false;
    };

    uint32_t convertToKeySym(std::string key);
    bool     isKeyPressed(uint32_t ks);
    KeyboardState getKeyboardState();
    void          setOverlayTyping(bool on);
}
