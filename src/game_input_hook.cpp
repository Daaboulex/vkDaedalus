#include "game_input_hook.hpp"

#include "input_hook.hpp"
#include "logger.hpp"

#include <wayland-client.h>
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>

#include <dlfcn.h>

#include <array>
#include <string>

namespace vkBasalt
{
    namespace
    {
        const VkBasaltInputHook* inputHook()
        {
            static const VkBasaltInputHook* const hook = []() -> const VkBasaltInputHook* {
                using Lookup      = const VkBasaltInputHook* (*)();
                const auto lookup = reinterpret_cast<Lookup>(dlsym(RTLD_DEFAULT, "vkdaedalus_input_hook"));
                if (lookup == nullptr)
                    return nullptr;
                const VkBasaltInputHook* found = lookup();
                if (found->version != vkbasaltInputHookVersion)
                {
                    Logger::warn("the preloaded libvkdaedalus-input-hook.so speaks version " + std::to_string(found->version) + " and this layer version "
                                 + std::to_string(vkbasaltInputHookVersion)
                                 + ", so game input is never blocked: start the game with this build's vkdaedalus-run");
                    return nullptr;
                }
                return found;
            }();
            return hook;
        }
    }

    int addOverlayListener(wl_proxy* proxy, const void* listener, void* data)
    {
        if (const VkBasaltInputHook* hook = inputHook())
            hook->markOwnProxy(proxy);
        return wl_proxy_add_listener(proxy, reinterpret_cast<void (**)(void)>(const_cast<void*>(listener)), data);
    }

    void unregisterOverlayProxy(wl_proxy* proxy)
    {
        if (const VkBasaltInputHook* hook = inputHook())
            hook->unmarkOwnProxy(proxy);
    }

    bool gameInputHooked()
    {
        const VkBasaltInputHook* hook = inputHook();
        return hook != nullptr && hook->gameListenersWrapped();
    }

    void setGameInputBlocked(bool blocked)
    {
        if (const VkBasaltInputHook* hook = inputHook())
            hook->setBlocked(blocked);
    }

    bool gameX11InputHooked()
    {
        const VkBasaltInputHook* hook = inputHook();
        return hook != nullptr && hook->xcbReady();
    }

    void markOverlayX11Display(void* display)
    {
        if (const VkBasaltInputHook* hook = inputHook(); hook != nullptr && display != nullptr)
            hook->markOwnXcbConnection(XGetXCBConnection(static_cast<Display*>(display)));
    }

    void setGameXInputOpcode(int opcode)
    {
        if (const VkBasaltInputHook* hook = inputHook())
            hook->setXInputOpcode(opcode);
    }

    void takeGameButtons(void (*into)(uint32_t button, bool pressed))
    {
        const VkBasaltInputHook* hook = inputHook();
        if (hook == nullptr)
            return;
        std::array<VkBasaltInputHookButton, 64> buttons;
        for (size_t taken = hook->takeGameButtons(buttons.data(), buttons.size()); taken > 0; taken = hook->takeGameButtons(buttons.data(), buttons.size()))
            for (size_t i = 0; i < taken; i++)
                into(buttons[i].button, buttons[i].pressed);
    }
}
