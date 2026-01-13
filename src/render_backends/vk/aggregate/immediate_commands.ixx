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
        
        VkCommandBuffer begin_single_time_commands();

        void end_single_time_commands();
        
        struct CommandPoolScope
        {
            CommandPoolScope(ImmediateCommandPool& in_icp)
                : icp(in_icp)
            {
                icp.begin_single_time_commands();
            }
            
            ~CommandPoolScope()
            {
                icp.end_single_time_commands();
            }
          
            ImmediateCommandPool& icp;
        };
        
        CommandPoolScope single_time_command_scope()
        {
            return {*this};
        }

        vk::Instance& instance;
        
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };
}
