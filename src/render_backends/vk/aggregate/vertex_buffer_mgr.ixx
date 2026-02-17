export module vk:vertex_buffer_mgr;

import <vulkan/vulkan_core.h>;

import render;
import <vector>;

namespace vk
{
    
    export struct VertexBufferInfo
    {
        VkBuffer buffer;
        VkDeviceMemory memory;

        void* mapped;
        bool dynamic;

        VkDeviceSize total_size;
        VkDeviceSize per_frame_size;
    };

    
    struct VertexBufferManager
    {
        VertexBufferManager(VkDevice& in_device, VkPhysicalDevice& in_physical_device)
            : device(in_device)
            , physical_device(in_physical_device)
        {}
        
        RBVertexBufferHandle create_vertex_buffer(const VertexBufferDesc& desc);
        void* get_mapped_pointer(RBVertexBufferHandle handle, RBFrameHandle frame);
        void bind_vertex_buffer(RBCommandList cmd, RBVertexBufferHandle handle, RBFrameHandle frame);

        VkDevice& device;
        VkPhysicalDevice& physical_device;
        
        std::vector<VertexBufferInfo> vertex_buffers;
    };
}
