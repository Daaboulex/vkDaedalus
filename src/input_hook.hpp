#pragma once

#include <cstddef>
#include <cstdint>

struct wl_proxy;

inline constexpr uint32_t vkbasaltInputHookVersion = 2;

struct VkBasaltInputHookButton
{
    uint32_t button  = 0;
    bool     pressed = false;
};

struct VkBasaltInputHook
{
    uint32_t version;
    void (*setBlocked)(bool blocked);
    bool (*gameListenersWrapped)();
    void (*markOwnProxy)(wl_proxy* proxy);
    void (*unmarkOwnProxy)(wl_proxy* proxy);
    size_t (*takeGameButtons)(VkBasaltInputHookButton* into, size_t capacity);
    bool (*xcbReady)();
    void (*markOwnXcbConnection)(const void* connection);
    void (*setXInputOpcode)(int opcode);
};
