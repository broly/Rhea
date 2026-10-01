module;

#include <vulkan/vulkan_core.h>

export module vk:immediate_commands;

import std.compat;
import :instance;

namespace vk
{
    // staging memory of an upload, written by the CPU and copied from by commands of record_upload
    struct UploadStaging
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        std::byte* data = nullptr;
    };

    class ImmediateCommandPool
    {
    public:
        ImmediateCommandPool(vk::Instance& in_instance)
            : instance(in_instance)
        {}
        
        void init();

        // Uploads (textures, meshes) go into a shared batch instead of one submit + wait each: staging memory from
        // persistent per-batch buffers, the batch is submitted before anything else reaches the graphics queue
        // (submit, the frame, wait_idle), so whatever runs after sees the data. The CPU waits only when it wraps
        // around onto a batch the GPU has not finished. allocate_upload, then record_upload of the copies at once
        // (an allocation may submit the open batch and start the next one).
        UploadStaging allocate_upload(VkDeviceSize size, VkDeviceSize alignment = 16);
        void record_upload(const std::function<void(VkCommandBuffer)>& commands);
        void flush_uploads();

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

    private:
        struct UploadBatch
        {
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            VkFence fence = VK_NULL_HANDLE;
            VkBuffer staging = VK_NULL_HANDLE;
            VkDeviceMemory staging_memory = VK_NULL_HANDLE;
            std::byte* mapped = nullptr;
            VkDeviceSize used = 0;
            bool recording = false;
            bool in_flight = false;
            // uploads larger than the staging buffer, freed when the batch is reused
            std::vector<std::pair<VkBuffer, VkDeviceMemory>> dedicated;
        };

        static constexpr uint32_t upload_batch_count = 4;
        static constexpr VkDeviceSize upload_batch_size = VkDeviceSize(32) << 20;

        UploadBatch& open_upload_batch();

        std::array<UploadBatch, upload_batch_count> upload_batches;
        uint32_t current_upload_batch = 0;
    };
}
