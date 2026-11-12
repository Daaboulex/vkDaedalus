#include "x11_input_filter.hpp"

#include <X11/Xproto.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

using namespace vkBasalt;

static int failures = 0;

static void expect(bool condition, const std::string& what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what.c_str());
    failures++;
}

static std::array<uint8_t, sizeof(xEvent)> core(uint8_t type, uint8_t detail)
{
    xEvent event{};
    event.u.u.type   = type;
    event.u.u.detail = detail;
    std::array<uint8_t, sizeof(xEvent)> bytes{};
    std::memcpy(bytes.data(), &event, sizeof(event));
    return bytes;
}

static std::array<uint8_t, sizeof(xXIRawEvent)> generic(uint8_t opcode, uint16_t evtype, uint32_t detail, uint32_t time = 0x5a5a5a5a)
{
    xXIRawEvent event{};
    event.type      = GenericEvent;
    event.extension = opcode;
    event.evtype    = evtype;
    event.detail    = detail;
    event.time      = time;
    std::array<uint8_t, sizeof(xXIRawEvent)> bytes{};
    std::memcpy(bytes.data(), &event, sizeof(event));
    return bytes;
}

static std::array<uint8_t, sizeof(xXIDeviceEvent)> device(uint8_t opcode, uint16_t evtype, uint32_t detail, uint32_t time)
{
    xXIDeviceEvent event{};
    event.type      = GenericEvent;
    event.extension = opcode;
    event.evtype    = evtype;
    event.detail    = detail;
    event.time      = time;
    std::array<uint8_t, sizeof(xXIDeviceEvent)> bytes{};
    std::memcpy(bytes.data(), &event, sizeof(event));
    return bytes;
}

int main()
{
    X11DroppedPresses dropped;
    const int         xi = 131;
    expect(dropX11Input(core(KeyPress, 38).data(), xi, dropped) && dropX11Input(core(KeyRelease, 38).data(), xi, dropped),
           "a key pressed while blocked is dropped, and so is its release");
    expect(!dropX11Input(core(KeyRelease, 40).data(), xi, dropped), "the release of a key pressed before blocking reaches the game, so it does not stick");
    expect(dropX11Input(core(ButtonPress | x11::sendEventBit, 1).data(), xi, dropped) && dropX11Input(core(ButtonRelease, 1).data(), xi, dropped),
           "a button press, sent or not, is dropped with its release");
    expect(dropX11Input(core(MotionNotify, 0).data(), xi, dropped), "pointer motion is dropped");
    expect(dropX11Input(core(EnterNotify, 0).data(), xi, dropped) && dropX11Input(core(LeaveNotify, 0).data(), xi, dropped)
               && dropX11Input(device(xi, XI_Enter, 0, 3000).data(), xi, dropped) && dropX11Input(device(xi, XI_Leave, 0, 3010).data(), xi, dropped),
           "the pointer crossing a window is dropped, core or XI2, since Wine turns an EnterNotify into a mouse move");
    expect(!dropX11Input(core(FocusIn, 0).data(), xi, dropped) && !dropX11Input(core(FocusOut, 0).data(), xi, dropped)
               && !dropX11Input(device(xi, XI_FocusIn, 0, 3020).data(), xi, dropped),
           "focus reaches the game, so the layer and the game agree on who has the keyboard");
    expect(!dropX11Input(core(12, 0).data(), xi, dropped) && !dropX11Input(core(22, 0).data(), xi, dropped), "an Expose or a ConfigureNotify reaches the game");
    expect(dropX11Input(generic(xi, XI_RawMotion, 0).data(), xi, dropped) && dropX11Input(generic(xi, XI_RawButtonPress, 3).data(), xi, dropped) &&
               dropX11Input(generic(xi, XI_RawButtonRelease, 3).data(), xi, dropped),
           "XI2 raw motion and raw buttons, which Wine reads for a clipped mouse, are dropped");
    expect(!dropX11Input(generic(xi, XI_RawKeyRelease, 50).data(), xi, dropped), "an XI2 key release whose press came before blocking reaches the game");
    expect(dropX11Input(generic(xi, XI_RawKeyPress, 24, 1000).data(), xi, dropped) && dropX11Input(generic(xi, XI_RawKeyRelease, 24, 1150).data(), xi, dropped),
           "a raw key pressed while blocked is dropped with its release, which comes at another time");
    expect(dropX11Input(device(xi, XI_KeyPress, 25, 2000).data(), xi, dropped) && dropX11Input(device(xi, XI_KeyRelease, 25, 2210).data(), xi, dropped)
               && !dropX11Input(device(xi, XI_KeyRelease, 26, 2210).data(), xi, dropped),
           "an XI2 key is paired by its key, not by when it came: its own release is dropped and another key's passes");
    expect(!dropX11Input(generic(xi + 1, XI_RawMotion, 0).data(), xi, dropped), "a generic event of another extension, such as Present, reaches the game");
    expect(!dropX11Input(generic(xi, XI_RawMotion, 0).data(), -1, dropped), "with the XInput opcode unknown, no generic event is dropped");
    dropX11Input(core(KeyPress, 9).data(), xi, dropped);
    dropped.clear();
    expect(!dropX11Input(core(KeyRelease, 9).data(), xi, dropped), "once blocking ends, a release reaches the game whatever was dropped before");
    return failures ? 1 : 0;
}
