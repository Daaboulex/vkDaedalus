#ifndef COMMAND_BUFFER_HPP_INCLUDED
#define COMMAND_BUFFER_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <memory>

#include "vulkan_include.hpp"

#include "logical_device.hpp"

#include "effects/effect.hpp"
namespace vkBasalt
{

    std::vector<VkCommandBuffer> recordCommandBuffers(LogicalDevice*                                        pLogicalDevice,
                                                      uint32_t                                              count,
                                                      const std::vector<std::shared_ptr<vkBasalt::Effect>>& effects,
                                                      VkImage                                               depthImage,
                                                      VkImageView                                           depthImageView,
                                                      VkFormat                                              depthFormat,
                                                      VkQueryPool                                           timingQueryPool = VK_NULL_HANDLE,
                                                      uint32_t                                              timedEffectCount = 0);

    std::vector<VkSemaphore> createSemaphores(LogicalDevice* pLogicalDevice, uint32_t count);
}

#endif
