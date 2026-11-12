#ifndef EFFECT_HPP_INCLUDED
#define EFFECT_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <unordered_map>
#include <memory>

#include "vulkan_include.hpp"
#include "params/effect_param.hpp"
#include "reshade_frame.hpp"

namespace vkBasalt
{
    class Effect
    {
    public:
        void virtual applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) = 0;
        virtual void updateEffect(uint32_t imageIndex, const ReshadeFrame& frame) {}
        void virtual useDepthImage(VkImageView depthImageView){};
        virtual ~Effect(){};

    private:
    };
}

#endif
