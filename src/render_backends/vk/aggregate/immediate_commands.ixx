export module vk:immediate_commands;
import :instance;
import <optional>;
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
        
        void submit(std::function<void(VkCommandBuffer)>&& commands);
        void submit(std::function<void(VkCommandBuffer)>&& commands, std::optional<RBCommandList> cmd);
        void submit(std::function<void(VkCommandBuffer)>&& commands, std::function<void()> finally);
        void submit(std::function<void(VkCommandBuffer)>&& commands, std::function<void()> finally, std::optional<RBCommandList> cmd);
        
        void add_release_callback(RBCommandList in_cmd, std::function<void()>&& callback);
        
        void on_begin_main_commands(RBCommandList cmd);
        
        void on_end_main_commands(RBCommandList cmd);
        
        VkCommandBuffer begin_single_time_commands();

        void end_single_time_commands();
        
        void flush_garbage(RBFrameHandle frame);
        
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
        
        RBCommandList active_main_command_buffer {};
        
        std::map<VkCommandBuffer, std::vector<std::function<void()>>> release_callbacks;
    };
}
