export module vk:instance;

import platform;
import <vulkan/vulkan_core.h>;
import <glfw/glfw3.h>;
import :context;

import :debug_object_tracker;

namespace vk
{
    export class Instance
    {
    public:
        Instance(VkDebugObjectTracker& in_debug_object_tracker)
            : debug_object_tracker(in_debug_object_tracker)
        {}
        
        void init(GLFWwindow* window);
        void match_queue_families();
        
        static VkBool32 debug_callback(
            VkDebugUtilsMessageSeverityFlagBitsEXT           messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT                  messageTypes,
            const VkDebugUtilsMessengerCallbackDataEXT*      pCallbackData,
            void*                                            pUserData);
        
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
        
        
        VkDebugObjectTracker& debug_object_tracker;
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        VkQueue graphics_queue = VK_NULL_HANDLE;
        VkQueue present_queue = VK_NULL_HANDLE;
        QueueFamilies queues;
        
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props {};
    
        uint32_t queues_indices[MAX_QUEUES];
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        GLFWwindow* window = nullptr;
    };
}
