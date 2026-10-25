#ifndef MOUSE_INPUT_HPP_INCLUDED
#define MOUSE_INPUT_HPP_INCLUDED

#include <cstdint>

namespace vkBasalt
{
    struct MouseState
    {
        int x = 0;
        int y = 0;
        bool leftButton = false;
        bool rightButton = false;
        bool middleButton = false;
        float scrollUpDelta = 0.0f;
    };

    void beginMouseInputFrame(bool effectsReadMouse);
    void setOverlayReadsMouse(bool reads);
    void discardPendingMouseInput();

    MouseState getMouseState();

    uint64_t mousePointerQueries();

}

#endif
