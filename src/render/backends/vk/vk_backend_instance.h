#pragma once
#include <vulkan/vulkan_core.h>

#include "vk_context.h"

namespace vk
{
    class Instance
    {
    public:
        void init(GLFWwindow* window);
        void match_queue_families();
        
        VkInstance instance;
        VkDevice device;
        VkPhysicalDevice physical_device;
        VkQueue graphics_queue;
        VkQueue present_queue;
        QueueFamilies queues;
    
        uint32_t queues_indices[MAX_QUEUES];
        VkSurfaceKHR surface;
    };
}
