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
