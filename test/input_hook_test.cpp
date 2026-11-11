#include "input-hook/driver.h"
#include "input_hook.hpp"

#include <wayland-client.h>

#include <dlfcn.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <initializer_list>
#include <vector>

static int          failures  = 0;
static int          serverEnd = -1;
static wl_display*  display   = nullptr;

static void expect(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    failures++;
}

static void send(void* to, uint16_t opcode, std::initializer_list<uint32_t> args)
{
    std::vector<uint32_t> words = {wl_proxy_get_id(static_cast<wl_proxy*>(to)), static_cast<uint32_t>((8 + 4 * args.size()) << 16) | opcode};
    words.insert(words.end(), args.begin(), args.end());
    const ssize_t bytes = static_cast<ssize_t>(words.size() * sizeof(uint32_t));
    expect(write(serverEnd, words.data(), static_cast<size_t>(bytes)) == bytes, "the test compositor's event is written");
    expect(wl_display_dispatch(display) >= 0, "the event is dispatched");
}

struct OwnCounts
{
    int motion = 0;
};

static const wl_pointer_listener ownListener = {
    .enter         = [](void*, wl_pointer*, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t) {},
    .leave         = [](void*, wl_pointer*, uint32_t, wl_surface*) {},
    .motion        = [](void* data, wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t) { static_cast<OwnCounts*>(data)->motion++; },
    .button        = [](void*, wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t) {},
    .axis          = [](void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t) {},
    .frame         = [](void*, wl_pointer*) {},
    .axis_source   = [](void*, wl_pointer*, uint32_t) {},
    .axis_stop     = [](void*, wl_pointer*, uint32_t, uint32_t) {},
    .axis_discrete = [](void*, wl_pointer*, uint32_t, int32_t) {},
};

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    using Lookup      = const VkBasaltInputHook* (*)();
    const auto lookup = reinterpret_cast<Lookup>(dlsym(RTLD_DEFAULT, "vkdaedalus_input_hook"));
    expect(lookup != nullptr, "libvkdaedalus-input-hook.so is preloaded and found by name, as the layer finds it");
    if (lookup == nullptr)
        return 1;
    const VkBasaltInputHook* hook = lookup();
    expect(hook->version == vkbasaltInputHookVersion, "the hook speaks the layer's version");

    int fds[2];
    expect(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) == 0, "a socket pair stands in for the compositor");
    serverEnd                 = fds[1];
    display                   = wl_display_connect_to_fd(fds[0]);
    wl_registry* registry     = wl_display_get_registry(display);
    auto*        seat         = static_cast<wl_seat*>(wl_registry_bind(registry, 1, &wl_seat_interface, 5));
    wl_pointer*  gamePointer  = wl_seat_get_pointer(seat);
    wl_keyboard* gameKeyboard = wl_seat_get_keyboard(seat);
    wl_pointer*  ownPointer   = wl_seat_get_pointer(seat);
    wl_display_flush(display);

    expect(!hook->gameListenersWrapped(), "before the game adds a listener nothing is wrapped");
    void* driver = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    expect(driver != nullptr, "the mock Wayland driver loads RTLD_LOCAL, as Wine loads winewayland.so");
    if (driver == nullptr)
        return 1;
    const auto    listen = reinterpret_cast<driver_listen_fn>(dlsym(driver, "driver_listen"));
    driver_counts counts{};
    listen(gamePointer, gameKeyboard, &counts);
    expect(hook->gameListenersWrapped(), "the listeners a driver adds at its start, before any layer loads, are wrapped");

    hook->markOwnProxy(reinterpret_cast<wl_proxy*>(ownPointer));
    OwnCounts own;
    wl_pointer_add_listener(ownPointer, &ownListener, &own);

    send(gamePointer, 2, {1, 10 << 8, 10 << 8});
    expect(counts.motion == 1, "unblocked, the game's pointer motion reaches it");

    hook->setBlocked(true);
    send(gamePointer, 2, {2, 20 << 8, 20 << 8});
    expect(counts.motion == 1, "blocked, pointer motion does not reach the game");
    send(gamePointer, 5, {});
    expect(counts.frame == 1, "blocked, a pointer frame still reaches the game");
    send(gamePointer, 3, {1, 3, 0x110, WL_POINTER_BUTTON_STATE_PRESSED});
    expect(counts.button == 0, "blocked, a button press does not reach the game");
    send(gamePointer, 3, {2, 4, 0x110, WL_POINTER_BUTTON_STATE_RELEASED});
    expect(counts.button == 1, "blocked, a button release still reaches the game, so a button held as the overlay opened does not stick");
    send(gameKeyboard, 3, {3, 5, 30, WL_KEYBOARD_KEY_STATE_PRESSED});
    expect(counts.key == 0, "blocked, a key press does not reach the game");
    send(gameKeyboard, 3, {4, 6, 30, WL_KEYBOARD_KEY_STATE_RELEASED});
    expect(counts.key == 1, "blocked, a key release still reaches the game, so a key held as the overlay opened does not stick");
    send(gameKeyboard, 4, {5, 0, 0, 0, 0});
    expect(counts.modifiers == 1, "blocked, the modifier state still reaches the game");
    send(ownPointer, 2, {7, 30 << 8, 30 << 8});
    expect(own.motion == 1, "the overlay's own pointer gets its motion while the game's is blocked");

    hook->setBlocked(false);
    send(gamePointer, 2, {8, 40 << 8, 40 << 8});
    expect(counts.motion == 2, "unblocked again, motion reaches the game");
    send(gamePointer, 3, {9, 7, 0x111, WL_POINTER_BUTTON_STATE_PRESSED});
    expect(counts.button == 2, "unblocked again, a button press reaches the game");

    VkBasaltInputHookButton taken[8];
    const size_t            buttons = hook->takeGameButtons(taken, 8);
    expect(buttons == 2 && taken[0].button == 0x110 && taken[0].pressed && !taken[1].pressed,
           "the game pointer's button events are kept for the overlay, in order, and only while it blocks the game");
    expect(hook->takeGameButtons(taken, 8) == 0, "and handed over once");

    const auto make = [seat](const wl_interface& kind) { return wl_proxy_create(reinterpret_cast<wl_proxy*>(seat), &kind); };
    wl_proxy*      surface = make(wl_surface_interface);
    wl_proxy*      tablet  = make(zwp_tablet_v2_interface);
    driver_inputs  inputs{};
    inputs.touch    = reinterpret_cast<wl_touch*>(make(wl_touch_interface));
    inputs.relative = reinterpret_cast<zwp_relative_pointer_v1*>(make(zwp_relative_pointer_v1_interface));
    inputs.swipe    = reinterpret_cast<zwp_pointer_gesture_swipe_v1*>(make(zwp_pointer_gesture_swipe_v1_interface));
    inputs.pinch    = reinterpret_cast<zwp_pointer_gesture_pinch_v1*>(make(zwp_pointer_gesture_pinch_v1_interface));
    inputs.hold     = reinterpret_cast<zwp_pointer_gesture_hold_v1*>(make(zwp_pointer_gesture_hold_v1_interface));
    inputs.tool     = reinterpret_cast<zwp_tablet_tool_v2*>(make(zwp_tablet_tool_v2_interface));
    inputs.pad      = reinterpret_cast<zwp_tablet_pad_v2*>(make(zwp_tablet_pad_v2_interface));
    inputs.ring     = reinterpret_cast<zwp_tablet_pad_ring_v2*>(make(zwp_tablet_pad_ring_v2_interface));
    inputs.strip    = reinterpret_cast<zwp_tablet_pad_strip_v2*>(make(zwp_tablet_pad_strip_v2_interface));
#ifdef ZWP_TABLET_PAD_DIAL_V2_DELTA_SINCE_VERSION
    inputs.dial = make(zwp_tablet_pad_dial_v2_interface);
#endif
    inputs.text = reinterpret_cast<zwp_text_input_v3*>(make(zwp_text_input_v3_interface));
    const auto listenInputs = reinterpret_cast<driver_listen_inputs_fn>(dlsym(driver, "driver_listen_inputs"));
    expect(listenInputs != nullptr, "the mock driver adds listeners to every other input kind");
    if (listenInputs == nullptr)
        return 1;
    listenInputs(&inputs, &counts);
    const uint32_t onSurface = wl_proxy_get_id(surface);
    const uint32_t onTablet  = wl_proxy_get_id(tablet);
    const uint32_t pressed   = ZWP_TABLET_TOOL_V2_BUTTON_STATE_PRESSED;
    const uint32_t released  = ZWP_TABLET_TOOL_V2_BUTTON_STATE_RELEASED;
    const uint32_t padDown   = ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED;
    const uint32_t padUp     = ZWP_TABLET_PAD_V2_BUTTON_STATE_RELEASED;

    send(inputs.relative, 0, {0, 1, 1 << 8, 1 << 8, 1 << 8, 1 << 8});
    expect(counts.relative_motion == 1, "unblocked, relative motion reaches the game through the hook");
    send(inputs.touch, 0, {10, 1, onSurface, 3, 5 << 8, 5 << 8});
    send(inputs.swipe, 0, {11, 2, onSurface, 3});
    send(inputs.pinch, 1, {3, 1 << 8, 1 << 8, 1 << 8, 0});
    send(inputs.hold, 0, {12, 4, onSurface, 2});
    send(inputs.tool, 6, {13, onTablet, onSurface});
    send(inputs.pad, 4, {5, 0, padDown});
    send(inputs.ring, 1, {90 << 8});
    send(inputs.strip, 1, {500});
    send(inputs.text, 3, {0});
    expect(counts.touch_down == 1 && counts.swipe_begin == 1 && counts.pinch_update == 1 && counts.hold_begin == 1 && counts.tool_proximity_in == 1
               && counts.pad_button == 1 && counts.ring_angle == 1 && counts.strip_position == 1 && counts.text_commit == 1,
           "unblocked, touch, gestures, the tablet tool, pad, ring and strip, and text input all reach the game through the hook");

    hook->setBlocked(true);
    send(inputs.relative, 0, {0, 2, 1 << 8, 1 << 8, 1 << 8, 1 << 8});
    expect(counts.relative_motion == 1, "blocked, relative motion, which a game's camera follows, does not reach the game");
    send(inputs.touch, 0, {14, 5, onSurface, 4, 6 << 8, 6 << 8});
    expect(counts.touch_down == 1, "blocked, a touch going down does not reach the game");
    send(inputs.touch, 1, {15, 6, 3});
    send(inputs.touch, 3, {});
    expect(counts.touch_up == 1 && counts.touch_frame == 1, "blocked, a touch coming up and its frame still reach the game, so a touch held as the overlay opened ends");
    send(inputs.swipe, 0, {16, 7, onSurface, 3});
    expect(counts.swipe_begin == 1, "blocked, a swipe does not begin in the game");
    send(inputs.swipe, 2, {17, 8, 0});
    expect(counts.swipe_end == 1, "blocked, a swipe's end still reaches the game");
    send(inputs.pinch, 1, {9, 1 << 8, 1 << 8, 1 << 8, 0});
    expect(counts.pinch_update == 1, "blocked, a pinch does not move in the game");
    send(inputs.pinch, 2, {18, 10, 1});
    expect(counts.pinch_end == 1, "blocked, a pinch's end still reaches the game");
    send(inputs.hold, 0, {19, 11, onSurface, 2});
    expect(counts.hold_begin == 1, "blocked, a hold gesture does not begin in the game");
    send(inputs.hold, 1, {20, 12, 0});
    expect(counts.hold_end == 1, "blocked, a hold gesture's end still reaches the game");
    send(inputs.tool, 6, {21, onTablet, onSurface});
    send(inputs.tool, 10, {2 << 8, 2 << 8});
    send(inputs.tool, 8, {22});
    expect(counts.tool_proximity_in == 1 && counts.tool_motion == 0 && counts.tool_down == 0,
           "blocked, a tablet tool coming near, moving or touching down does not reach the game");
    send(inputs.tool, 9, {});
    send(inputs.tool, 18, {13});
    expect(counts.tool_up == 1 && counts.tool_frame == 1, "blocked, a tablet tool lifting and its frame still reach the game");
    send(inputs.tool, 17, {23, 0x14b, pressed});
    expect(counts.tool_button == 0, "blocked, a tablet tool button press does not reach the game");
    send(inputs.tool, 17, {24, 0x14b, released});
    expect(counts.tool_button == 1, "blocked, a tablet tool button release still reaches the game");
    send(inputs.pad, 4, {14, 0, padDown});
    expect(counts.pad_button == 1, "blocked, a tablet pad button press does not reach the game");
    send(inputs.pad, 4, {15, 0, padUp});
    expect(counts.pad_button == 2, "blocked, a tablet pad button release still reaches the game");
    send(inputs.ring, 1, {180 << 8});
    expect(counts.ring_angle == 1, "blocked, a pad ring turning does not reach the game");
    send(inputs.ring, 2, {});
    expect(counts.ring_stop == 1, "blocked, a pad ring stopping still reaches the game");
    send(inputs.strip, 1, {600});
    expect(counts.strip_position == 1, "blocked, a pad strip moving does not reach the game");
#ifdef ZWP_TABLET_PAD_DIAL_V2_DELTA_SINCE_VERSION
    send(inputs.dial, 0, {120});
    expect(counts.dial_delta == 0, "blocked, a pad dial turning does not reach the game");
#endif
    send(inputs.text, 3, {0});
    expect(counts.text_commit == 1, "blocked, text the input method commits does not reach the game");
    send(inputs.text, 5, {25});
    expect(counts.text_done == 1, "blocked, the input method's done still reaches the game, so its state stays in step");

    hook->setBlocked(false);
    send(inputs.relative, 0, {0, 3, 1 << 8, 1 << 8, 1 << 8, 1 << 8});
    send(inputs.tool, 10, {3 << 8, 3 << 8});
#ifdef ZWP_TABLET_PAD_DIAL_V2_DELTA_SINCE_VERSION
    send(inputs.dial, 0, {120});
    expect(counts.dial_delta == 1, "unblocked again, a pad dial reaches the game");
#endif
    expect(counts.relative_motion == 2 && counts.tool_motion == 1, "unblocked again, relative and tablet motion reach the game");

    for (wl_proxy* made : {surface, tablet, reinterpret_cast<wl_proxy*>(inputs.touch), reinterpret_cast<wl_proxy*>(inputs.relative),
                           reinterpret_cast<wl_proxy*>(inputs.swipe), reinterpret_cast<wl_proxy*>(inputs.pinch), reinterpret_cast<wl_proxy*>(inputs.hold),
                           reinterpret_cast<wl_proxy*>(inputs.tool), reinterpret_cast<wl_proxy*>(inputs.pad), reinterpret_cast<wl_proxy*>(inputs.ring),
                           reinterpret_cast<wl_proxy*>(inputs.strip), inputs.dial, reinterpret_cast<wl_proxy*>(inputs.text)})
        if (made != nullptr)
            wl_proxy_destroy(made);

    wl_pointer_destroy(ownPointer);
    wl_keyboard_destroy(gameKeyboard);
    wl_pointer_destroy(gamePointer);
    wl_seat_destroy(seat);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    close(serverEnd);
    dlclose(driver);
    if (failures != 0)
    {
        std::printf("%d input hook check(s) failed\n", failures);
        return 1;
    }
    std::printf("OK: the game's listeners are wrapped from its start and blocked only while asked\n");
    return 0;
}
