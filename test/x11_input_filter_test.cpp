#include "x11_input_filter.hpp"

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

static std::array<uint8_t, 32> core(uint8_t type, uint8_t detail)
{
    std::array<uint8_t, 32> event{};
    event[0] = type;
    event[1] = detail;
    return event;
}

static std::array<uint8_t, 32> generic(uint8_t opcode, uint16_t evtype, uint32_t detail)
{
    std::array<uint8_t, 32> event{};
    event[0] = x11::genericEvent;
    event[1] = opcode;
    std::memcpy(event.data() + x11::genericEventTypeAt, &evtype, sizeof(evtype));
    std::memcpy(event.data() + x11::xiDetailAt, &detail, sizeof(detail));
    return event;
}

int main()
{
    X11DroppedPresses dropped;
    const int         xi = 131;
    expect(dropX11Input(core(x11::keyPress, 38).data(), xi, dropped) && dropX11Input(core(x11::keyRelease, 38).data(), xi, dropped),
           "a key pressed while blocked is dropped, and so is its release");
    expect(!dropX11Input(core(x11::keyRelease, 40).data(), xi, dropped), "the release of a key pressed before blocking reaches the game, so it does not stick");
    expect(dropX11Input(core(x11::buttonPress | x11::sendEventBit, 1).data(), xi, dropped) && dropX11Input(core(x11::buttonRelease, 1).data(), xi, dropped),
           "a button press, sent or not, is dropped with its release");
    expect(dropX11Input(core(x11::motionNotify, 0).data(), xi, dropped), "pointer motion is dropped");
    expect(!dropX11Input(core(12, 0).data(), xi, dropped) && !dropX11Input(core(22, 0).data(), xi, dropped), "an Expose or a ConfigureNotify reaches the game");
    expect(dropX11Input(generic(xi, x11::xiRawMotion, 0).data(), xi, dropped) && dropX11Input(generic(xi, x11::xiRawButtonPress, 3).data(), xi, dropped) &&
               dropX11Input(generic(xi, x11::xiRawButtonRelease, 3).data(), xi, dropped),
           "XI2 raw motion and raw buttons, which Wine reads for a clipped mouse, are dropped");
    expect(!dropX11Input(generic(xi, x11::xiRawKeyRelease, 50).data(), xi, dropped), "an XI2 key release whose press came before blocking reaches the game");
    expect(!dropX11Input(generic(xi + 1, x11::xiRawMotion, 0).data(), xi, dropped), "a generic event of another extension, such as Present, reaches the game");
    expect(!dropX11Input(generic(xi, x11::xiRawMotion, 0).data(), -1, dropped), "with the XInput opcode unknown, no generic event is dropped");
    dropX11Input(core(x11::keyPress, 9).data(), xi, dropped);
    dropped.clear();
    expect(!dropX11Input(core(x11::keyRelease, 9).data(), xi, dropped), "once blocking ends, a release reaches the game whatever was dropped before");
    return failures ? 1 : 0;
}
