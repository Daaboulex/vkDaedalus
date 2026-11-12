#pragma once

#include <X11/X.h>
#include <X11/extensions/XI2.h>
#include <X11/extensions/XI2proto.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace vkBasalt
{
    namespace x11
    {
        inline constexpr uint8_t sendEventBit       = 0x80;
        inline constexpr size_t  genericEventTypeAt = offsetof(xXIRawEvent, evtype);
        inline constexpr size_t  xiDetailAt         = offsetof(xXIRawEvent, detail);
        static_assert(offsetof(xXIDeviceEvent, evtype) == genericEventTypeAt && offsetof(xXIDeviceEvent, detail) == xiDetailAt,
                      "XInput2 device and raw events keep their type and detail at the same place");
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
            case KeyPress: dropped.keys[detail] = true; return true;
            case KeyRelease: return std::exchange(dropped.keys[detail], false);
            case ButtonPress: dropped.buttons[detail] = true; return true;
            case ButtonRelease: return std::exchange(dropped.buttons[detail], false);
            case MotionNotify: return true;
            case GenericEvent:
            {
                if (xinputOpcode < 0 || event[1] != xinputOpcode)
                    return false;
                uint16_t evtype   = 0;
                uint32_t xiDetail = 0;
                std::memcpy(&evtype, event + x11::genericEventTypeAt, sizeof(evtype));
                std::memcpy(&xiDetail, event + x11::xiDetailAt, sizeof(xiDetail));
                const size_t at = xiDetail & 0xFF;
                if (evtype == XI_KeyPress || evtype == XI_RawKeyPress)
                    return dropped.xiKeys[at] = true;
                if (evtype == XI_KeyRelease || evtype == XI_RawKeyRelease)
                    return std::exchange(dropped.xiKeys[at], false);
                if (evtype == XI_ButtonPress || evtype == XI_RawButtonPress)
                    return dropped.xiButtons[at] = true;
                if (evtype == XI_ButtonRelease || evtype == XI_RawButtonRelease)
                    return std::exchange(dropped.xiButtons[at], false);
                return evtype == XI_Motion || evtype == XI_RawMotion || (evtype >= XI_TouchBegin && evtype <= XI_TouchEnd) ||
                       (evtype >= XI_RawTouchBegin && evtype <= XI_RawTouchEnd);
            }
            default: return false;
        }
    }
}
