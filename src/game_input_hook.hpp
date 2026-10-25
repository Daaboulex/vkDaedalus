#pragma once

#include <cstdint>

struct wl_proxy;

namespace vkBasalt
{
    int  addOverlayListener(wl_proxy* proxy, const void* listener, void* data);
    void unregisterOverlayProxy(wl_proxy* proxy);
    bool gameInputHooked();
    void setGameInputBlocked(bool blocked);
    void takeGameButtons(void (*into)(uint32_t button, bool pressed));
    bool gameX11InputHooked();
    void markOverlayX11Display(void* display);
    void setGameXInputOpcode(int opcode);
}
