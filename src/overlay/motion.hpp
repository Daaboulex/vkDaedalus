#ifndef MOTION_HPP_INCLUDED
#define MOTION_HPP_INCLUDED

#include <cmath>

namespace vkBasalt
{
    struct Motion
    {
        static constexpr float hoverSeconds = 0.04f;
        static constexpr float cardSeconds  = 0.05f;
        static constexpr float settled      = 0.001f;
    };

    inline float approach(float current, float target, float elapsedSeconds, float timeConstantSeconds)
    {
        if (!std::isfinite(target))
            return std::isfinite(current) ? current : 0.0f;
        if (!std::isfinite(current) || !(timeConstantSeconds > 0.0f))
            return target;
        if (!(elapsedSeconds > 0.0f))
            return current;
        const float next = target + (current - target) * std::exp(-elapsedSeconds / timeConstantSeconds);
        return std::fabs(next - target) < Motion::settled ? target : next;
    }
}

#endif
