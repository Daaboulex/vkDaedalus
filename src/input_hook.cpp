#include "input_hook.hpp"
#include "prefix_holder.hpp"
#include "wine_process.hpp"
#include "x11_input_filter.hpp"

#include "pointer-gestures-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "tablet-v2-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"

#include <X11/Xlib.h>
#include <wayland-client.h>
#include <xcb/xcb.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

    template<typename Listener>
    struct GameListener
    {
        const Listener* listener = nullptr;
        void*           data     = nullptr;
    };

    template<typename Proxy, typename Listener>
    using Listeners = std::unordered_map<Proxy*, GameListener<Listener>>;

    std::mutex                                                                         mutex;
    Listeners<wl_pointer, wl_pointer_listener>                                         pointers;
    Listeners<wl_keyboard, wl_keyboard_listener>                                       keyboards;
    Listeners<wl_touch, wl_touch_listener>                                             touches;
    Listeners<zwp_relative_pointer_v1, zwp_relative_pointer_v1_listener>               relativePointers;
    Listeners<zwp_pointer_gesture_swipe_v1, zwp_pointer_gesture_swipe_v1_listener>     swipes;
    Listeners<zwp_pointer_gesture_pinch_v1, zwp_pointer_gesture_pinch_v1_listener>     pinches;
    Listeners<zwp_pointer_gesture_hold_v1, zwp_pointer_gesture_hold_v1_listener>       holds;
    Listeners<zwp_tablet_tool_v2, zwp_tablet_tool_v2_listener>                         tabletTools;
    Listeners<zwp_tablet_pad_v2, zwp_tablet_pad_v2_listener>                           tabletPads;
    Listeners<zwp_tablet_pad_ring_v2, zwp_tablet_pad_ring_v2_listener>                 padRings;
    Listeners<zwp_tablet_pad_strip_v2, zwp_tablet_pad_strip_v2_listener>               padStrips;
#ifdef ZWP_TABLET_PAD_DIAL_V2_DELTA_SINCE_VERSION
    Listeners<zwp_tablet_pad_dial_v2, zwp_tablet_pad_dial_v2_listener>                 padDials;
#endif
    Listeners<zwp_text_input_v3, zwp_text_input_v3_listener>                           textInputs;
    std::unordered_set<wl_proxy*>                                                      ownProxies;
    std::array<VkBasaltInputHookButton, 64>                                            buttons;
    size_t                                                                             buttonCount = 0;
    std::atomic<bool>                                                                  blocked{false};

    template<typename Proxy, typename Listener>
    std::optional<GameListener<Listener>> entryOf(const Listeners<Proxy, Listener>& table, Proxy* proxy)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto                  at = table.find(proxy);
        if (at == table.end())
            return std::nullopt;
        return at->second;
    }

    template<auto Member, typename Proxy, typename Listener, typename... Args>
    void toGame(const Listeners<Proxy, Listener>& table, Proxy* proxy, Args... args)
    {
        if (const std::optional<GameListener<Listener>> game = entryOf(table, proxy); game && game->listener->*Member)
            (game->listener->*Member)(game->data, proxy, args...);
    }

    bool isBlocked()
    {
        return blocked.load(std::memory_order_acquire);
    }

    template<auto Member, auto& Table, typename Proxy, typename... Args>
    void pass(void*, Proxy* proxy, Args... args)
    {
        toGame<Member>(Table, proxy, args...);
    }

    template<auto Member, auto& Table, typename Proxy, typename... Args>
    void holdWhileBlocked(void*, Proxy* proxy, Args... args)
    {
        if (!isBlocked())
            toGame<Member>(Table, proxy, args...);
    }

    void recordButton(uint32_t button, bool pressed)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (buttonCount < buttons.size())
            buttons[buttonCount++] = {button, pressed};
    }

    void pointerButton(void*, wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
    {
        if (isBlocked())
            recordButton(button, state == WL_POINTER_BUTTON_STATE_PRESSED);
        if (!isBlocked() || state == WL_POINTER_BUTTON_STATE_RELEASED)
            toGame<&wl_pointer_listener::button>(pointers, pointer, serial, time, button, state);
    }

    const wl_pointer_listener pointerWrapper = {
        .enter         = pass<&wl_pointer_listener::enter, pointers>,
        .leave         = pass<&wl_pointer_listener::leave, pointers>,
        .motion        = holdWhileBlocked<&wl_pointer_listener::motion, pointers>,
        .button        = pointerButton,
        .axis          = holdWhileBlocked<&wl_pointer_listener::axis, pointers>,
        .frame         = pass<&wl_pointer_listener::frame, pointers>,
        .axis_source   = holdWhileBlocked<&wl_pointer_listener::axis_source, pointers>,
        .axis_stop     = holdWhileBlocked<&wl_pointer_listener::axis_stop, pointers>,
        .axis_discrete = holdWhileBlocked<&wl_pointer_listener::axis_discrete, pointers>,
#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
        .axis_value120 = holdWhileBlocked<&wl_pointer_listener::axis_value120, pointers>,
#endif
#ifdef WL_POINTER_AXIS_RELATIVE_DIRECTION_SINCE_VERSION
        .axis_relative_direction = holdWhileBlocked<&wl_pointer_listener::axis_relative_direction, pointers>,
#endif
#ifdef WL_POINTER_WARP_SINCE_VERSION
        .warp = holdWhileBlocked<&wl_pointer_listener::warp, pointers>,
#endif
    };

    void keyboardKey(void*, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
    {
        if (!isBlocked() || state == WL_KEYBOARD_KEY_STATE_RELEASED)
            toGame<&wl_keyboard_listener::key>(keyboards, keyboard, serial, time, key, state);
    }

    const wl_keyboard_listener keyboardWrapper = {
        .keymap      = pass<&wl_keyboard_listener::keymap, keyboards>,
        .enter       = pass<&wl_keyboard_listener::enter, keyboards>,
        .leave       = pass<&wl_keyboard_listener::leave, keyboards>,
        .key         = keyboardKey,
        .modifiers   = pass<&wl_keyboard_listener::modifiers, keyboards>,
        .repeat_info = pass<&wl_keyboard_listener::repeat_info, keyboards>,
    };

    const wl_touch_listener touchWrapper = {
        .down        = holdWhileBlocked<&wl_touch_listener::down, touches>,
        .up          = pass<&wl_touch_listener::up, touches>,
        .motion      = holdWhileBlocked<&wl_touch_listener::motion, touches>,
        .frame       = pass<&wl_touch_listener::frame, touches>,
        .cancel      = pass<&wl_touch_listener::cancel, touches>,
        .shape       = holdWhileBlocked<&wl_touch_listener::shape, touches>,
        .orientation = holdWhileBlocked<&wl_touch_listener::orientation, touches>,
    };

    const zwp_relative_pointer_v1_listener relativePointerWrapper = {
        .relative_motion = holdWhileBlocked<&zwp_relative_pointer_v1_listener::relative_motion, relativePointers>,
    };

    const zwp_pointer_gesture_swipe_v1_listener swipeWrapper = {
        .begin  = holdWhileBlocked<&zwp_pointer_gesture_swipe_v1_listener::begin, swipes>,
        .update = holdWhileBlocked<&zwp_pointer_gesture_swipe_v1_listener::update, swipes>,
        .end    = pass<&zwp_pointer_gesture_swipe_v1_listener::end, swipes>,
    };

    const zwp_pointer_gesture_pinch_v1_listener pinchWrapper = {
        .begin  = holdWhileBlocked<&zwp_pointer_gesture_pinch_v1_listener::begin, pinches>,
        .update = holdWhileBlocked<&zwp_pointer_gesture_pinch_v1_listener::update, pinches>,
        .end    = pass<&zwp_pointer_gesture_pinch_v1_listener::end, pinches>,
    };

    const zwp_pointer_gesture_hold_v1_listener holdWrapper = {
        .begin = holdWhileBlocked<&zwp_pointer_gesture_hold_v1_listener::begin, holds>,
        .end   = pass<&zwp_pointer_gesture_hold_v1_listener::end, holds>,
    };

    void tabletToolButton(void*, zwp_tablet_tool_v2* tool, uint32_t serial, uint32_t button, uint32_t state)
    {
        if (!isBlocked() || state == ZWP_TABLET_TOOL_V2_BUTTON_STATE_RELEASED)
            toGame<&zwp_tablet_tool_v2_listener::button>(tabletTools, tool, serial, button, state);
    }

    const zwp_tablet_tool_v2_listener tabletToolWrapper = {
        .type              = pass<&zwp_tablet_tool_v2_listener::type, tabletTools>,
        .hardware_serial   = pass<&zwp_tablet_tool_v2_listener::hardware_serial, tabletTools>,
        .hardware_id_wacom = pass<&zwp_tablet_tool_v2_listener::hardware_id_wacom, tabletTools>,
        .capability        = pass<&zwp_tablet_tool_v2_listener::capability, tabletTools>,
        .done              = pass<&zwp_tablet_tool_v2_listener::done, tabletTools>,
        .removed           = pass<&zwp_tablet_tool_v2_listener::removed, tabletTools>,
        .proximity_in      = holdWhileBlocked<&zwp_tablet_tool_v2_listener::proximity_in, tabletTools>,
        .proximity_out     = pass<&zwp_tablet_tool_v2_listener::proximity_out, tabletTools>,
        .down              = holdWhileBlocked<&zwp_tablet_tool_v2_listener::down, tabletTools>,
        .up                = pass<&zwp_tablet_tool_v2_listener::up, tabletTools>,
        .motion            = holdWhileBlocked<&zwp_tablet_tool_v2_listener::motion, tabletTools>,
        .pressure          = holdWhileBlocked<&zwp_tablet_tool_v2_listener::pressure, tabletTools>,
        .distance          = holdWhileBlocked<&zwp_tablet_tool_v2_listener::distance, tabletTools>,
        .tilt              = holdWhileBlocked<&zwp_tablet_tool_v2_listener::tilt, tabletTools>,
        .rotation          = holdWhileBlocked<&zwp_tablet_tool_v2_listener::rotation, tabletTools>,
        .slider            = holdWhileBlocked<&zwp_tablet_tool_v2_listener::slider, tabletTools>,
        .wheel             = holdWhileBlocked<&zwp_tablet_tool_v2_listener::wheel, tabletTools>,
        .button            = tabletToolButton,
        .frame             = pass<&zwp_tablet_tool_v2_listener::frame, tabletTools>,
    };

    void tabletPadButton(void*, zwp_tablet_pad_v2* pad, uint32_t time, uint32_t button, uint32_t state)
    {
        if (!isBlocked() || state == ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED)
            toGame<&zwp_tablet_pad_v2_listener::button>(tabletPads, pad, time, button, state);
    }

    const zwp_tablet_pad_v2_listener tabletPadWrapper = {
        .group   = pass<&zwp_tablet_pad_v2_listener::group, tabletPads>,
        .path    = pass<&zwp_tablet_pad_v2_listener::path, tabletPads>,
        .buttons = pass<&zwp_tablet_pad_v2_listener::buttons, tabletPads>,
        .done    = pass<&zwp_tablet_pad_v2_listener::done, tabletPads>,
        .button  = tabletPadButton,
        .enter   = pass<&zwp_tablet_pad_v2_listener::enter, tabletPads>,
        .leave   = pass<&zwp_tablet_pad_v2_listener::leave, tabletPads>,
        .removed = pass<&zwp_tablet_pad_v2_listener::removed, tabletPads>,
    };

    const zwp_tablet_pad_ring_v2_listener padRingWrapper = {
        .source = pass<&zwp_tablet_pad_ring_v2_listener::source, padRings>,
        .angle  = holdWhileBlocked<&zwp_tablet_pad_ring_v2_listener::angle, padRings>,
        .stop   = pass<&zwp_tablet_pad_ring_v2_listener::stop, padRings>,
        .frame  = pass<&zwp_tablet_pad_ring_v2_listener::frame, padRings>,
    };

    const zwp_tablet_pad_strip_v2_listener padStripWrapper = {
        .source   = pass<&zwp_tablet_pad_strip_v2_listener::source, padStrips>,
        .position = holdWhileBlocked<&zwp_tablet_pad_strip_v2_listener::position, padStrips>,
        .stop     = pass<&zwp_tablet_pad_strip_v2_listener::stop, padStrips>,
        .frame    = pass<&zwp_tablet_pad_strip_v2_listener::frame, padStrips>,
    };

#ifdef ZWP_TABLET_PAD_DIAL_V2_DELTA_SINCE_VERSION
    const zwp_tablet_pad_dial_v2_listener padDialWrapper = {
        .delta = holdWhileBlocked<&zwp_tablet_pad_dial_v2_listener::delta, padDials>,
        .frame = pass<&zwp_tablet_pad_dial_v2_listener::frame, padDials>,
    };
#endif

    const zwp_text_input_v3_listener textInputWrapper = {
        .enter                   = pass<&zwp_text_input_v3_listener::enter, textInputs>,
        .leave                   = pass<&zwp_text_input_v3_listener::leave, textInputs>,
        .preedit_string          = holdWhileBlocked<&zwp_text_input_v3_listener::preedit_string, textInputs>,
        .commit_string           = holdWhileBlocked<&zwp_text_input_v3_listener::commit_string, textInputs>,
        .delete_surrounding_text = holdWhileBlocked<&zwp_text_input_v3_listener::delete_surrounding_text, textInputs>,
        .done                    = pass<&zwp_text_input_v3_listener::done, textInputs>,
#ifdef ZWP_TEXT_INPUT_V3_ACTION_SINCE_VERSION
        .action = holdWhileBlocked<&zwp_text_input_v3_listener::action, textInputs>,
#endif
#ifdef ZWP_TEXT_INPUT_V3_LANGUAGE_SINCE_VERSION
        .language = pass<&zwp_text_input_v3_listener::language, textInputs>,
#endif
#ifdef ZWP_TEXT_INPUT_V3_PREEDIT_HINT_SINCE_VERSION
        .preedit_hint = holdWhileBlocked<&zwp_text_input_v3_listener::preedit_hint, textInputs>,
#endif
    };

    template<auto& Table, auto& Wrapper>
    int wrapListener(AddListener addListener, wl_proxy* proxy, void (**implementation)(void), void* data)
    {
        using Proxy    = std::remove_pointer_t<typename std::remove_reference_t<decltype(Table)>::key_type>;
        using Listener = std::remove_cv_t<std::remove_reference_t<decltype(Wrapper)>>;
        {
            std::lock_guard<std::mutex> lock(mutex);
            Table[reinterpret_cast<Proxy*>(proxy)] = {reinterpret_cast<const Listener*>(implementation), data};
        }
        return addListener(proxy, reinterpret_cast<void (**)(void)>(const_cast<Listener*>(&Wrapper)), nullptr);
    }

    struct Wrappable
    {
        const char* name;
        int (*wrap)(AddListener, wl_proxy*, void (**)(void), void*);
    };

    const Wrappable wrappable[] = {
        {"wl_pointer", wrapListener<pointers, pointerWrapper>},
        {"wl_keyboard", wrapListener<keyboards, keyboardWrapper>},
        {"wl_touch", wrapListener<touches, touchWrapper>},
        {"zwp_relative_pointer_v1", wrapListener<relativePointers, relativePointerWrapper>},
        {"zwp_pointer_gesture_swipe_v1", wrapListener<swipes, swipeWrapper>},
        {"zwp_pointer_gesture_pinch_v1", wrapListener<pinches, pinchWrapper>},
        {"zwp_pointer_gesture_hold_v1", wrapListener<holds, holdWrapper>},
        {"zwp_tablet_tool_v2", wrapListener<tabletTools, tabletToolWrapper>},
        {"zwp_tablet_pad_v2", wrapListener<tabletPads, tabletPadWrapper>},
        {"zwp_tablet_pad_ring_v2", wrapListener<padRings, padRingWrapper>},
        {"zwp_tablet_pad_strip_v2", wrapListener<padStrips, padStripWrapper>},
#ifdef ZWP_TABLET_PAD_DIAL_V2_DELTA_SINCE_VERSION
        {"zwp_tablet_pad_dial_v2", wrapListener<padDials, padDialWrapper>},
#endif
        {"zwp_text_input_v3", wrapListener<textInputs, textInputWrapper>},
    };

    using XcbRead             = xcb_generic_event_t* (*)(xcb_connection_t*);
    using XcbGrabPointer      = xcb_grab_pointer_cookie_t (*)(xcb_connection_t*, uint8_t, xcb_window_t, uint16_t, uint8_t, uint8_t, xcb_window_t, xcb_cursor_t,
                                                         xcb_timestamp_t);
    using XcbGrabPointerReply = xcb_grab_pointer_reply_t* (*)(xcb_connection_t*, xcb_grab_pointer_cookie_t, xcb_generic_error_t**);
    using XcbUngrabPointer    = xcb_void_cookie_t (*)(xcb_connection_t*, xcb_timestamp_t);
    using XcbFlush            = int (*)(xcb_connection_t*);

    struct Xcb
    {
        XcbRead             poll             = nullptr;
        XcbRead             pollQueued       = nullptr;
        XcbRead             wait             = nullptr;
        XcbGrabPointer      grabPointer      = nullptr;
        XcbGrabPointerReply grabPointerReply = nullptr;
        XcbUngrabPointer    ungrabPointer    = nullptr;
        XcbFlush            flush            = nullptr;
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
            functions.grabPointer      = reinterpret_cast<XcbGrabPointer>(dlsym(from, "xcb_grab_pointer"));
            functions.grabPointerReply = reinterpret_cast<XcbGrabPointerReply>(dlsym(from, "xcb_grab_pointer_reply"));
            functions.ungrabPointer    = reinterpret_cast<XcbUngrabPointer>(dlsym(from, "xcb_ungrab_pointer"));
            functions.flush            = reinterpret_cast<XcbFlush>(dlsym(from, "xcb_flush"));
            return functions;
        }();
        return found;
    }

    std::unordered_set<const void*> ownConnections;

    struct HeldPointer
    {
        int          rootX = 0;
        int          rootY = 0;
        Window       child = None;
        unsigned int mask  = 0;
    };
    std::unordered_map<const Display*, HeldPointer> heldPointers;
    std::optional<HeldPointer>                      pointerAtBlock;

    struct GamePointerGrab
    {
        Window       window       = None;
        bool         ownerEvents  = false;
        unsigned int eventMask    = 0;
        int          pointerMode  = GrabModeAsync;
        int          keyboardMode = GrabModeAsync;
        Window       confineTo    = None;
        Cursor       cursor       = None;
    };
    std::mutex                                    grabMutex;
    std::unordered_map<Display*, GamePointerGrab> gamePointerGrabs;

    using QueryPointer         = Bool (*)(Display*, Window, Window*, Window*, int*, int*, int*, int*, unsigned int*);
    using WarpPointer          = int (*)(Display*, Window, Window, int, int, unsigned int, unsigned int, int, int);
    using TranslateCoordinates = Bool (*)(Display*, Window, Window, int, int, int*, int*, Window*);
    using ConnectionOf         = xcb_connection_t* (*)(Display*);
    using GrabPointer          = int (*)(Display*, Window, Bool, unsigned int, int, int, Window, Cursor, Time);
    using UngrabPointer        = int (*)(Display*, Time);
    using CloseDisplay         = int (*)(Display*);

    struct Xlib
    {
        QueryPointer         query        = nullptr;
        WarpPointer          warp         = nullptr;
        TranslateCoordinates translate    = nullptr;
        ConnectionOf         connectionOf = nullptr;
        GrabPointer          grab         = nullptr;
        UngrabPointer        ungrab       = nullptr;
        CloseDisplay         close        = nullptr;
    };

    const Xlib& xlib()
    {
        static const Xlib found = [] {
            Xlib        functions;
            void* const x11  = dlopen("libX11.so.6", RTLD_NOW | RTLD_NOLOAD);
            void* const from = x11 != nullptr ? x11 : RTLD_NEXT;
            void* const xcb  = dlopen("libX11-xcb.so.1", RTLD_NOW | RTLD_NOLOAD);
            functions.query        = reinterpret_cast<QueryPointer>(dlsym(from, "XQueryPointer"));
            functions.warp         = reinterpret_cast<WarpPointer>(dlsym(from, "XWarpPointer"));
            functions.translate    = reinterpret_cast<TranslateCoordinates>(dlsym(from, "XTranslateCoordinates"));
            functions.grab         = reinterpret_cast<GrabPointer>(dlsym(from, "XGrabPointer"));
            functions.ungrab       = reinterpret_cast<UngrabPointer>(dlsym(from, "XUngrabPointer"));
            functions.close        = reinterpret_cast<CloseDisplay>(dlsym(from, "XCloseDisplay"));
            functions.connectionOf = reinterpret_cast<ConnectionOf>(dlsym(xcb != nullptr ? xcb : RTLD_DEFAULT, "XGetXCBConnection"));
            return functions;
        }();
        return found;
    }

    bool ownDisplay(Display* display)
    {
        const ConnectionOf connectionOf = xlib().connectionOf;
        std::lock_guard<std::mutex> lock(mutex);
        return connectionOf != nullptr && ownConnections.count(connectionOf(display)) > 0;
    }
    std::atomic<int>                xinputOpcode{-1};
    vkBasalt::X11DroppedPresses     droppedPresses;
    vkBasalt::X11GameKeys           gameKeys;

    struct PrefixInput
    {
        alignas(std::atomic_ref<uint64_t>::required_alignment) uint64_t holder;
        uint32_t                                                        holds;
        int32_t                                                         xinputOpcode;
    };

    PrefixInput* mapPrefixInput()
    {
        std::array<char, PATH_MAX> exe{};
        const ssize_t              length = readlink("/proc/self/exe", exe.data(), exe.size() - 1);
        if (length <= 0)
            return nullptr;
        const std::string_view path(exe.data(), static_cast<size_t>(length));
        if (!vkBasalt::isWineLoader(path.substr(path.find_last_of('/') + 1)))
            return nullptr;
        const std::string prefix  = vkBasalt::winePrefixFrom(std::getenv("WINEPREFIX"), std::getenv("HOME"));
        const char*       runtime = std::getenv("XDG_RUNTIME_DIR");
        struct stat       folder  = {};
        if (prefix.empty() || runtime == nullptr || *runtime == '\0' || stat(prefix.c_str(), &folder) != 0)
        {
            std::fprintf(stderr,
                         "vkdaedalus input hook: no XDG_RUNTIME_DIR or no Wine prefix at '%s', so the raw input this Wine process reads for its prefix "
                         "is never held for the game\n",
                         prefix.c_str());
            return nullptr;
        }
        const std::string name = std::string(runtime) + "/vkdaedalus-input-" + std::to_string(folder.st_dev) + "-" + std::to_string(folder.st_ino);
        const int         fd   = open(name.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        struct stat       file = {};
        void*             mapped = MAP_FAILED;
        if (fd >= 0 && fstat(fd, &file) == 0 && (file.st_size >= static_cast<off_t>(sizeof(PrefixInput)) || ftruncate(fd, sizeof(PrefixInput)) == 0))
            mapped = mmap(nullptr, sizeof(PrefixInput), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        const int error = errno;
        if (fd >= 0)
            close(fd);
        if (mapped == MAP_FAILED)
        {
            std::fprintf(stderr, "vkdaedalus input hook: %s could not be shared (%s), so the raw input this Wine process reads for its prefix is never held for the game\n",
                         name.c_str(), std::strerror(error));
            return nullptr;
        }
        return static_cast<PrefixInput*>(mapped);
    }

    PrefixInput* prefixInput()
    {
        static PrefixInput* const shared = mapPrefixInput();
        return shared;
    }

    template<typename Field>
    std::atomic_ref<Field> sharedField(Field& field)
    {
        static_assert(std::atomic_ref<Field>::is_always_lock_free, "a field other processes map is read and written without a lock");
        return std::atomic_ref<Field>(field);
    }

    std::optional<std::string> statLineOf(const std::string& process)
    {
        const int fd = open(("/proc/" + process + "/stat").c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0)
            return std::nullopt;
        std::array<char, 1024> line{};
        const ssize_t          length = read(fd, line.data(), line.size());
        close(fd);
        return length > 0 ? std::optional<std::string>(std::string(line.data(), static_cast<size_t>(length))) : std::nullopt;
    }

    constexpr std::chrono::milliseconds   holderCheckInterval{100};
    uint64_t                              holderChecked = 0;
    std::chrono::steady_clock::time_point holderCheckedAt{};
    uint64_t                              heldAs = 0;

    bool prefixHoldsInput()
    {
        PrefixInput* const prefix = prefixInput();
        if (prefix == nullptr)
            return false;
        uint64_t holder = sharedField(prefix->holder).load(std::memory_order_acquire);
        if (holder == 0 || vkBasalt::prefixHolderPid(holder) == getpid())
            return false;
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (holder == holderChecked && now - holderCheckedAt < holderCheckInterval)
                return true;
        }
        const std::optional<std::string> stat = statLineOf(std::to_string(vkBasalt::prefixHolderPid(holder)));
        if (!vkBasalt::prefixHolderAlive(holder, stat ? std::optional<std::string_view>(*stat) : std::nullopt))
        {
            sharedField(prefix->holder).compare_exchange_strong(holder, 0, std::memory_order_acq_rel);
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        holderChecked   = holder;
        holderCheckedAt = now;
        return true;
    }

    int knownXInputOpcode()
    {
        const int own = xinputOpcode.load(std::memory_order_acquire);
        if (own >= 0)
            return own;
        PrefixInput* const prefix = prefixInput();
        const int32_t      shared = prefix != nullptr ? sharedField(prefix->xinputOpcode).load(std::memory_order_acquire) : 0;
        return shared > 0 ? shared : -1;
    }

    uint32_t holdSeen = 0;

    bool dropWhileBlocked(xcb_connection_t* connection, const xcb_generic_event_t* event)
    {
        if (event == nullptr)
            return false;
        const uint8_t* bytes  = reinterpret_cast<const uint8_t*>(event);
        const int      opcode = knownXInputOpcode();
        const bool     own    = blocked.load(std::memory_order_acquire);
        const bool     prefix = !own && prefixHoldsInput();
        if (!own && !prefix && !vkBasalt::x11KeyOf(bytes, opcode))
            return false;
        std::lock_guard<std::mutex> lock(mutex);
        if (ownConnections.count(connection) > 0)
            return false;
        if (!prefix)
            return vkBasalt::filterX11Input(bytes, opcode, own, droppedPresses, gameKeys);
        if (const uint32_t hold = sharedField(prefixInput()->holds).load(std::memory_order_acquire); hold != holdSeen)
        {
            droppedPresses.clear();
            holdSeen = hold;
        }
        return vkBasalt::filterX11Input(bytes, opcode, vkBasalt::isX11RawInput(bytes, opcode), droppedPresses, gameKeys);
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
        if (PrefixInput* const prefix = prefixInput())
            sharedField(prefix->xinputOpcode).store(opcode, std::memory_order_release);
    }

    void releaseGamePointerGrabs()
    {
        const Xcb&         calls        = xcb();
        const ConnectionOf connectionOf = xlib().connectionOf;
        if (calls.ungrabPointer == nullptr || calls.flush == nullptr || connectionOf == nullptr)
            return;
        for (const auto& [display, grab] : gamePointerGrabs)
        {
            xcb_connection_t* const connection = connectionOf(display);
            calls.ungrabPointer(connection, XCB_CURRENT_TIME);
            calls.flush(connection);
        }
    }

    void restoreGamePointerGrabs()
    {
        const Xcb&         calls        = xcb();
        const ConnectionOf connectionOf = xlib().connectionOf;
        if (calls.grabPointer == nullptr || calls.grabPointerReply == nullptr || connectionOf == nullptr)
            return;
        for (const auto& [display, grab] : gamePointerGrabs)
        {
            xcb_connection_t* const         connection = connectionOf(display);
            xcb_generic_error_t*            error      = nullptr;
            const xcb_grab_pointer_cookie_t asked      = calls.grabPointer(
                connection, grab.ownerEvents, static_cast<xcb_window_t>(grab.window), static_cast<uint16_t>(grab.eventMask), static_cast<uint8_t>(grab.pointerMode),
                static_cast<uint8_t>(grab.keyboardMode), static_cast<xcb_window_t>(grab.confineTo), static_cast<xcb_cursor_t>(grab.cursor), XCB_CURRENT_TIME);
            xcb_grab_pointer_reply_t* const reply = calls.grabPointerReply(connection, asked, &error);
            if (error != nullptr)
                std::fprintf(stderr, "vkdaedalus input hook: the game's pointer grab of window 0x%lx was not given back when the overlay closed: X error %u\n", grab.window,
                             static_cast<unsigned>(error->error_code));
            else if (reply == nullptr || reply->status != XCB_GRAB_STATUS_SUCCESS)
                std::fprintf(stderr, "vkdaedalus input hook: the game's pointer grab of window 0x%lx was not given back when the overlay closed: grab status %d\n", grab.window,
                             reply != nullptr ? static_cast<int>(reply->status) : -1);
            std::free(reply);
            std::free(error);
        }
    }

    void setBlocked(bool block)
    {
        if (block)
        {
            blocked.store(true, std::memory_order_release);
            std::lock_guard<std::mutex> grabLock(grabMutex);
            releaseGamePointerGrabs();
        }
        else
        {
            std::lock_guard<std::mutex> grabLock(grabMutex);
            restoreGamePointerGrabs();
            blocked.store(false, std::memory_order_release);
        }
        PrefixInput* const prefix = prefixInput();
        if (block && prefix != nullptr)
        {
            sharedField(prefix->holds).fetch_add(1, std::memory_order_acq_rel);
            const std::optional<std::string>   stat  = statLineOf("self");
            const std::optional<uint64_t>      start = stat ? vkBasalt::processStartTicks(*stat) : std::nullopt;
            if (start)
            {
                heldAs = vkBasalt::prefixHolderOf(getpid(), *start);
                sharedField(prefix->holder).store(heldAs, std::memory_order_release);
            }
            else
                std::fprintf(stderr, "vkdaedalus input hook: /proc/self/stat gives no start time, so the raw input this Wine prefix reads elsewhere is not held for the game\n");
        }
        if (!block)
        {
            if (uint64_t self = std::exchange(heldAs, 0); prefix != nullptr && self != 0)
                sharedField(prefix->holder).compare_exchange_strong(self, 0, std::memory_order_acq_rel);
            std::lock_guard<std::mutex> lock(mutex);
            droppedPresses.clear();
            heldPointers.clear();
            pointerAtBlock.reset();
        }
    }

    void holdPointerAt(int rootX, int rootY, unsigned int mask)
    {
        std::lock_guard<std::mutex> lock(mutex);
        pointerAtBlock = HeldPointer{rootX, rootY, None, mask};
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
                                           takeGameButtons,          xcbReady,     markOwnXcbConnection, setXInputOpcode, holdPointerAt};
    return &hook;
}

extern "C" __attribute__((visibility("default"))) Bool XQueryPointer(Display* display, Window window, Window* root, Window* child, int* rootX, int* rootY,
                                                                     int* windowX, int* windowY, unsigned int* mask)
{
    const QueryPointer query = xlib().query;
    if (query == nullptr)
        return False;
    const Bool found = query(display, window, root, child, rootX, rootY, windowX, windowY, mask);
    if (!found || !blocked.load(std::memory_order_acquire) || ownDisplay(display))
        return found;
    std::lock_guard<std::mutex> lock(mutex);
    const auto [held, first] = heldPointers.try_emplace(
        display, pointerAtBlock ? HeldPointer{pointerAtBlock->rootX, pointerAtBlock->rootY, *child, pointerAtBlock->mask} : HeldPointer{*rootX, *rootY, *child, *mask});
    const int dx = held->second.rootX - *rootX;
    const int dy = held->second.rootY - *rootY;
    *rootX += dx;
    *rootY += dy;
    *windowX += dx;
    *windowY += dy;
    *child = held->second.child;
    *mask  = held->second.mask;
    return found;
}

extern "C" __attribute__((visibility("default"))) int XWarpPointer(Display* display, Window from, Window to, int fromX, int fromY, unsigned int fromWidth,
                                                                   unsigned int fromHeight, int toX, int toY)
{
    const Xlib& functions = xlib();
    if (functions.warp == nullptr)
        return 0;
    if (!blocked.load(std::memory_order_acquire) || ownDisplay(display))
        return functions.warp(display, from, to, fromX, fromY, fromWidth, fromHeight, toX, toY);
    int    rootX = 0, rootY = 0;
    Window child = None;
    const bool placed = to != None && functions.translate != nullptr
                        && functions.translate(display, to, DefaultRootWindow(display), toX, toY, &rootX, &rootY, &child);
    std::lock_guard<std::mutex> lock(mutex);
    auto held = heldPointers.find(display);
    if (held == heldPointers.end() && pointerAtBlock)
        held = heldPointers.emplace(display, *pointerAtBlock).first;
    if (placed)
        heldPointers[display] = HeldPointer{rootX, rootY, held != heldPointers.end() ? held->second.child : None,
                                            held != heldPointers.end() ? held->second.mask : 0};
    else if (to == None && held != heldPointers.end())
    {
        held->second.rootX += toX;
        held->second.rootY += toY;
    }
    return 1;
}

extern "C" __attribute__((visibility("default"))) int XGrabPointer(Display* display, Window window, Bool ownerEvents, unsigned int eventMask, int pointerMode,
                                                                   int keyboardMode, Window confineTo, Cursor cursor, Time time)
{
    const Xlib& functions = xlib();
    if (functions.grab == nullptr)
        return GrabNotViewable;
    if (ownDisplay(display))
        return functions.grab(display, window, ownerEvents, eventMask, pointerMode, keyboardMode, confineTo, cursor, time);
    const GamePointerGrab       asked{window, ownerEvents != False, eventMask, pointerMode, keyboardMode, confineTo, cursor};
    std::lock_guard<std::mutex> grabLock(grabMutex);
    if (blocked.load(std::memory_order_acquire))
    {
        gamePointerGrabs.insert_or_assign(display, asked);
        return GrabSuccess;
    }
    const int status = functions.grab(display, window, ownerEvents, eventMask, pointerMode, keyboardMode, confineTo, cursor, time);
    if (status == GrabSuccess)
        gamePointerGrabs.insert_or_assign(display, asked);
    return status;
}

extern "C" __attribute__((visibility("default"))) int XUngrabPointer(Display* display, Time time)
{
    const Xlib& functions = xlib();
    if (functions.ungrab == nullptr)
        return 0;
    if (!ownDisplay(display))
    {
        std::lock_guard<std::mutex> grabLock(grabMutex);
        gamePointerGrabs.erase(display);
    }
    return functions.ungrab(display, time);
}

extern "C" __attribute__((visibility("default"))) int XCloseDisplay(Display* display)
{
    const Xlib& functions = xlib();
    if (functions.close == nullptr)
        return 0;
    {
        std::lock_guard<std::mutex> grabLock(grabMutex);
        gamePointerGrabs.erase(display);
    }
    return functions.close(display);
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
    for (const Wrappable& kind : wrappable)
        if (std::strcmp(name, kind.name) == 0)
            return kind.wrap(functions.addListener, proxy, implementation, data);
    return functions.addListener(proxy, implementation, data);
}
