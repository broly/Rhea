export module vk:immediate_commands;
import :instance;
import <vulkan/vulkan_core.h>;
import <functional>;

namespace vk
{
    class ImmediateCommandPool
    {
    public:
        ImmediateCommandPool(vk::Instance& in_instance)
            : instance(in_instance)
        {}
        
        void init();
        
        void submit(std::function<void(VkCommandBuffer)>&& fn);
        
        vk::Instance& instance;
        
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };
}
