#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

namespace vkBasalt
{
    namespace x11
    {
        inline constexpr uint8_t keyPress       = 2;
        inline constexpr uint8_t keyRelease     = 3;
        inline constexpr uint8_t buttonPress    = 4;
        inline constexpr uint8_t buttonRelease  = 5;
        inline constexpr uint8_t motionNotify   = 6;
        inline constexpr uint8_t genericEvent   = 35;
        inline constexpr uint8_t sendEventBit   = 0x80;

        inline constexpr uint16_t xiKeyPress         = 2;
        inline constexpr uint16_t xiKeyRelease       = 3;
        inline constexpr uint16_t xiButtonPress      = 4;
        inline constexpr uint16_t xiButtonRelease    = 5;
        inline constexpr uint16_t xiMotion           = 6;
        inline constexpr uint16_t xiRawKeyPress      = 13;
        inline constexpr uint16_t xiRawKeyRelease    = 14;
        inline constexpr uint16_t xiRawButtonPress   = 15;
        inline constexpr uint16_t xiRawButtonRelease = 16;
        inline constexpr uint16_t xiRawMotion        = 17;
        inline constexpr uint16_t xiTouchBegin       = 18;
        inline constexpr uint16_t xiTouchEnd         = 20;
        inline constexpr uint16_t xiRawTouchBegin    = 22;
        inline constexpr uint16_t xiRawTouchEnd      = 24;

        inline constexpr size_t genericEventTypeAt = 8;
        inline constexpr size_t xiDetailAt         = 12;
        inline constexpr size_t eventSize          = 32;
    }

    struct X11DroppedPresses
    {
        std::array<bool, 256> keys{};
        std::array<bool, 256> buttons{};
        std::array<bool, 256> xiKeys{};
        std::array<bool, 256> xiButtons{};

        void clear()
        {
            keys.fill(false);
            buttons.fill(false);
            xiKeys.fill(false);
            xiButtons.fill(false);
        }
    };

    inline bool dropX11Input(const uint8_t* event, int xinputOpcode, X11DroppedPresses& dropped)
    {
        const uint8_t type   = event[0] & static_cast<uint8_t>(~x11::sendEventBit);
        const uint8_t detail = event[1];
        switch (type)
        {
            case x11::keyPress: dropped.keys[detail] = true; return true;
            case x11::keyRelease: return std::exchange(dropped.keys[detail], false);
            case x11::buttonPress: dropped.buttons[detail] = true; return true;
            case x11::buttonRelease: return std::exchange(dropped.buttons[detail], false);
            case x11::motionNotify: return true;
            case x11::genericEvent:
            {
                if (xinputOpcode < 0 || event[1] != xinputOpcode)
                    return false;
                uint16_t evtype   = 0;
                uint32_t xiDetail = 0;
                std::memcpy(&evtype, event + x11::genericEventTypeAt, sizeof(evtype));
                std::memcpy(&xiDetail, event + x11::xiDetailAt, sizeof(xiDetail));
                const size_t at = xiDetail & 0xFF;
                if (evtype == x11::xiKeyPress || evtype == x11::xiRawKeyPress)
                    return dropped.xiKeys[at] = true;
                if (evtype == x11::xiKeyRelease || evtype == x11::xiRawKeyRelease)
                    return std::exchange(dropped.xiKeys[at], false);
                if (evtype == x11::xiButtonPress || evtype == x11::xiRawButtonPress)
                    return dropped.xiButtons[at] = true;
                if (evtype == x11::xiButtonRelease || evtype == x11::xiRawButtonRelease)
                    return std::exchange(dropped.xiButtons[at], false);
                return evtype == x11::xiMotion || evtype == x11::xiRawMotion || (evtype >= x11::xiTouchBegin && evtype <= x11::xiTouchEnd) ||
                       (evtype >= x11::xiRawTouchBegin && evtype <= x11::xiRawTouchEnd);
            }
            default: return false;
        }
    }
}
