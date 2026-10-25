#include "input_hook.hpp"
#include "x11_input_filter.hpp"

#include <wayland-client.h>
#include <xcb/xcb.h>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace
{
    using AddListener = int (*)(wl_proxy*, void (**)(void), void*);
    using GetClass    = const char* (*)(wl_proxy*);

    struct Wayland
    {
        AddListener addListener = nullptr;
        GetClass    getClass    = nullptr;
    };

    const Wayland& wayland()
    {
        static const Wayland found = [] {
            Wayland      functions;
            void* const  library = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_NOLOAD);
            void* const  from    = library != nullptr ? library : RTLD_NEXT;
            functions.addListener = reinterpret_cast<AddListener>(dlsym(from, "wl_proxy_add_listener"));
            functions.getClass    = reinterpret_cast<GetClass>(dlsym(from, "wl_proxy_get_class"));
            if (functions.addListener == nullptr || functions.getClass == nullptr)
                std::fprintf(stderr, "vkdaedalus input hook: libwayland-client's wl_proxy_add_listener was not found, so no listener can be added\n");
            return functions;
        }();
        return found;
    }

    struct GamePointer
    {
        const wl_pointer_listener* listener = nullptr;
        void*                      data     = nullptr;
    };

    struct GameKeyboard
    {
        const wl_keyboard_listener* listener = nullptr;
        void*                       data     = nullptr;
    };

    std::mutex                                     mutex;
    std::unordered_map<wl_pointer*, GamePointer>   pointers;
    std::unordered_map<wl_keyboard*, GameKeyboard> keyboards;
    std::unordered_set<wl_proxy*>                  ownProxies;
    std::array<VkBasaltInputHookButton, 64>        buttons;
    size_t                                         buttonCount = 0;
    std::atomic<bool>                              blocked{false};

    template<typename Proxy, typename Entry>
    std::optional<Entry> entryOf(const std::unordered_map<Proxy*, Entry>& table, Proxy* proxy)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto                  at = table.find(proxy);
        if (at == table.end())
            return std::nullopt;
        return at->second;
    }

    template<auto Member, typename... Args>
    void toGamePointer(wl_pointer* pointer, Args... args)
    {
        if (const std::optional<GamePointer> game = entryOf(pointers, pointer); game && game->listener->*Member)
            (game->listener->*Member)(game->data, pointer, args...);
    }

    template<auto Member, typename... Args>
    void toGameKeyboard(wl_keyboard* keyboard, Args... args)
    {
        if (const std::optional<GameKeyboard> game = entryOf(keyboards, keyboard); game && game->listener->*Member)
            (game->listener->*Member)(game->data, keyboard, args...);
    }

    bool isBlocked()
    {
        return blocked.load(std::memory_order_acquire);
    }

    void recordButton(uint32_t button, bool pressed)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (buttonCount < buttons.size())
            buttons[buttonCount++] = {button, pressed};
    }

    void pointerEnter(void*, wl_pointer* pointer, uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y)
    {
        toGamePointer<&wl_pointer_listener::enter>(pointer, serial, surface, x, y);
    }

    void pointerLeave(void*, wl_pointer* pointer, uint32_t serial, wl_surface* surface)
    {
        toGamePointer<&wl_pointer_listener::leave>(pointer, serial, surface);
    }

    void pointerMotion(void*, wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
    {
        if (!isBlocked())
            toGamePointer<&wl_pointer_listener::motion>(pointer, time, x, y);
    }

    void pointerButton(void*, wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
    {
        if (isBlocked())
            recordButton(button, state == WL_POINTER_BUTTON_STATE_PRESSED);
        if (!isBlocked() || state == WL_POINTER_BUTTON_STATE_RELEASED)
            toGamePointer<&wl_pointer_listener::button>(pointer, serial, time, button, state);
    }

    void pointerAxis(void*, wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
    {
        if (!isBlocked())
            toGamePointer<&wl_pointer_listener::axis>(pointer, time, axis, value);
    }

    void pointerFrame(void*, wl_pointer* pointer)
    {
        toGamePointer<&wl_pointer_listener::frame>(pointer);
    }

    void pointerAxisSource(void*, wl_pointer* pointer, uint32_t source)
    {
        if (!isBlocked())
            toGamePointer<&wl_pointer_listener::axis_source>(pointer, source);
    }

    void pointerAxisStop(void*, wl_pointer* pointer, uint32_t time, uint32_t axis)
    {
        if (!isBlocked())
            toGamePointer<&wl_pointer_listener::axis_stop>(pointer, time, axis);
    }

    void pointerAxisDiscrete(void*, wl_pointer* pointer, uint32_t axis, int32_t discrete)
    {
        if (!isBlocked())
            toGamePointer<&wl_pointer_listener::axis_discrete>(pointer, axis, discrete);
    }

#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
    void pointerAxisValue120(void*, wl_pointer* pointer, uint32_t axis, int32_t value120)
    {
        if (!isBlocked())
            toGamePointer<&wl_pointer_listener::axis_value120>(pointer, axis, value120);
    }
#endif

#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
    void pointerAxisRelativeDirection(void*, wl_pointer* pointer, uint32_t axis, uint32_t direction)
    {
        if (!isBlocked())
            toGamePointer<&wl_pointer_listener::axis_relative_direction>(pointer, axis, direction);
    }
#endif

#ifdef WL_POINTER_WARP_SINCE_VERSION
    void pointerWarp(void*, wl_pointer* pointer, wl_fixed_t x, wl_fixed_t y)
    {
        if (!isBlocked())
            toGamePointer<&wl_pointer_listener::warp>(pointer, x, y);
    }
#endif

    const wl_pointer_listener pointerWrapper = {
        .enter         = pointerEnter,
        .leave         = pointerLeave,
        .motion        = pointerMotion,
        .button        = pointerButton,
        .axis          = pointerAxis,
        .frame         = pointerFrame,
        .axis_source   = pointerAxisSource,
        .axis_stop     = pointerAxisStop,
        .axis_discrete = pointerAxisDiscrete,
#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
        .axis_value120 = pointerAxisValue120,
#endif
#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
        .axis_relative_direction = pointerAxisRelativeDirection,
#endif
#ifdef WL_POINTER_WARP_SINCE_VERSION
        .warp = pointerWarp,
#endif
    };

    void keyboardKeymap(void*, wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size)
    {
        toGameKeyboard<&wl_keyboard_listener::keymap>(keyboard, format, fd, size);
    }

    void keyboardEnter(void*, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys)
    {
        toGameKeyboard<&wl_keyboard_listener::enter>(keyboard, serial, surface, keys);
    }

    void keyboardLeave(void*, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface)
    {
        toGameKeyboard<&wl_keyboard_listener::leave>(keyboard, serial, surface);
    }

    void keyboardKey(void*, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
    {
        if (!isBlocked() || state == WL_KEYBOARD_KEY_STATE_RELEASED)
            toGameKeyboard<&wl_keyboard_listener::key>(keyboard, serial, time, key, state);
    }

    void keyboardModifiers(void*, wl_keyboard* keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
    {
        toGameKeyboard<&wl_keyboard_listener::modifiers>(keyboard, serial, depressed, latched, locked, group);
    }

    void keyboardRepeatInfo(void*, wl_keyboard* keyboard, int32_t rate, int32_t delay)
    {
        toGameKeyboard<&wl_keyboard_listener::repeat_info>(keyboard, rate, delay);
    }

    const wl_keyboard_listener keyboardWrapper = {
        .keymap      = keyboardKeymap,
        .enter       = keyboardEnter,
        .leave       = keyboardLeave,
        .key         = keyboardKey,
        .modifiers   = keyboardModifiers,
        .repeat_info = keyboardRepeatInfo,
    };

    using XcbRead = xcb_generic_event_t* (*)(xcb_connection_t*);

    struct Xcb
    {
        XcbRead poll       = nullptr;
        XcbRead pollQueued = nullptr;
        XcbRead wait       = nullptr;
    };

    const Xcb& xcb()
    {
        static const Xcb found = [] {
            Xcb         functions;
            void* const library = dlopen("libxcb.so.1", RTLD_NOW | RTLD_NOLOAD);
            void* const from    = library != nullptr ? library : RTLD_NEXT;
            functions.poll       = reinterpret_cast<XcbRead>(dlsym(from, "xcb_poll_for_event"));
            functions.pollQueued = reinterpret_cast<XcbRead>(dlsym(from, "xcb_poll_for_queued_event"));
            functions.wait       = reinterpret_cast<XcbRead>(dlsym(from, "xcb_wait_for_event"));
            return functions;
        }();
        return found;
    }

    std::unordered_set<const void*> ownConnections;
    std::atomic<int>                xinputOpcode{-1};
    vkBasalt::X11DroppedPresses     droppedPresses;

    bool dropWhileBlocked(xcb_connection_t* connection, const xcb_generic_event_t* event)
    {
        if (event == nullptr || !blocked.load(std::memory_order_acquire))
            return false;
        std::lock_guard<std::mutex> lock(mutex);
        if (ownConnections.count(connection) > 0)
            return false;
        return vkBasalt::dropX11Input(reinterpret_cast<const uint8_t*>(event), xinputOpcode.load(std::memory_order_acquire), droppedPresses);
    }

    xcb_generic_event_t* readKept(XcbRead read, xcb_connection_t* connection)
    {
        if (read == nullptr)
            return nullptr;
        for (;;)
        {
            xcb_generic_event_t* event = read(connection);
            if (!dropWhileBlocked(connection, event))
                return event;
            std::free(event);
        }
    }

    bool xcbReady()
    {
        return xcb().poll != nullptr && xcb().pollQueued != nullptr && xcb().wait != nullptr;
    }

    void markOwnXcbConnection(const void* connection)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ownConnections.insert(connection);
    }

    void setXInputOpcode(int opcode)
    {
        xinputOpcode.store(opcode, std::memory_order_release);
    }

    void setBlocked(bool block)
    {
        blocked.store(block, std::memory_order_release);
        if (!block)
        {
            std::lock_guard<std::mutex> lock(mutex);
            droppedPresses.clear();
        }
    }

    bool gameListenersWrapped()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return !pointers.empty() || !keyboards.empty();
    }

    void markOwnProxy(wl_proxy* proxy)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ownProxies.insert(proxy);
    }

    void unmarkOwnProxy(wl_proxy* proxy)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ownProxies.erase(proxy);
    }

    size_t takeGameButtons(VkBasaltInputHookButton* into, size_t capacity)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const size_t                taken = std::min(buttonCount, capacity);
        std::copy(buttons.begin(), buttons.begin() + static_cast<std::ptrdiff_t>(taken), into);
        std::copy(buttons.begin() + static_cast<std::ptrdiff_t>(taken), buttons.begin() + static_cast<std::ptrdiff_t>(buttonCount), buttons.begin());
        buttonCount -= taken;
        return taken;
    }

    bool isOwnProxy(wl_proxy* proxy)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return ownProxies.count(proxy) > 0;
    }
}

extern "C" __attribute__((visibility("default"))) const VkBasaltInputHook* vkdaedalus_input_hook()
{
    static const VkBasaltInputHook hook = {vkbasaltInputHookVersion, setBlocked,   gameListenersWrapped, markOwnProxy,   unmarkOwnProxy,
                                           takeGameButtons,          xcbReady,     markOwnXcbConnection, setXInputOpcode};
    return &hook;
}

extern "C" __attribute__((visibility("default"))) xcb_generic_event_t* xcb_poll_for_event(xcb_connection_t* connection)
{
    return readKept(xcb().poll, connection);
}

extern "C" __attribute__((visibility("default"))) xcb_generic_event_t* xcb_poll_for_queued_event(xcb_connection_t* connection)
{
    return readKept(xcb().pollQueued, connection);
}

extern "C" __attribute__((visibility("default"))) xcb_generic_event_t* xcb_wait_for_event(xcb_connection_t* connection)
{
    return readKept(xcb().wait, connection);
}

extern "C" __attribute__((visibility("default"))) int wl_proxy_add_listener(wl_proxy* proxy, void (**implementation)(void), void* data)
{
    const Wayland& functions = wayland();
    if (functions.addListener == nullptr || functions.getClass == nullptr)
        return -1;
    const char* name = functions.getClass(proxy);
    if (name == nullptr || isOwnProxy(proxy))
        return functions.addListener(proxy, implementation, data);
    if (std::strcmp(name, "wl_pointer") == 0)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            pointers[reinterpret_cast<wl_pointer*>(proxy)] = {reinterpret_cast<const wl_pointer_listener*>(implementation), data};
        }
        return functions.addListener(proxy, reinterpret_cast<void (**)(void)>(const_cast<wl_pointer_listener*>(&pointerWrapper)), nullptr);
    }
    if (std::strcmp(name, "wl_keyboard") == 0)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            keyboards[reinterpret_cast<wl_keyboard*>(proxy)] = {reinterpret_cast<const wl_keyboard_listener*>(implementation), data};
        }
        return functions.addListener(proxy, reinterpret_cast<void (**)(void)>(const_cast<wl_keyboard_listener*>(&keyboardWrapper)), nullptr);
    }
    return functions.addListener(proxy, implementation, data);
}
