export module vk:instance;

import platform;
import <vulkan/vulkan_core.h>;
import <glfw/glfw3.h>;
import :context;

namespace vk
{
    export class Instance
    {
    public:
        void init(GLFWwindow* window);
        void match_queue_families();
        
        VkDeviceSize get_non_coherent_atom_size() const;
        
        const VkDevice& get_device() const
        {
            return device;
        }
        
        const VkPhysicalDevice& get_physical_device() const
        {
            return physical_device;
        }
        
        const auto& get_rt_props() const
        {
            return rt_props;
        }
        
    public:
        
        
        
        VkInstance instance;
        VkDevice device;
        VkPhysicalDevice physical_device;
        VkQueue graphics_queue;
        VkQueue present_queue;
        QueueFamilies queues;
        
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props;
    
        uint32_t queues_indices[MAX_QUEUES];
        VkSurfaceKHR surface;
        GLFWwindow* window;
    };
}
