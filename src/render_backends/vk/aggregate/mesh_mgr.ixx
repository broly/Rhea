export module vk:mesh_mgr;

import :instance;
import :immediate_commands;
import :buffer_mgr;
import assets;
import <unordered_map>;
import <vulkan/vulkan_core.h>;

export struct MeshGPUData {
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory index_memory = VK_NULL_HANDLE;
    uint32_t index_count = 0;
    RBDescriptorSet descriptor_set;
    
    // BLAS
    VkAccelerationStructureKHR blas;
    VkDeviceMemory blas_memory;
    VkBuffer blas_buffer;
    VkDeviceAddress blas_address;
};


namespace vk
{
    class MeshManager
    {
    public:
        MeshManager(vk::Instance& in_instance, vk::ImmediateCommandPool& in_command_pool, vk::BufferManager& in_buffer_mgr)
            : instance(in_instance)
            , command_pool(in_command_pool)
            , buffer_manager(in_buffer_mgr)
        {}
        vk::Instance& instance;
        vk::ImmediateCommandPool& command_pool;
        vk::BufferManager& buffer_manager;
        
        
        void get_or_create_mesh_buffers(MeshPrimHandle handle, RTBuildMode rt_build_mode);
        
        void build_blas(
            MeshGPUData& data,
            VkDeviceAddress vertex_address,
            VkDeviceAddress index_address);
        
        void bind(const RBCommandList& cmd, MeshPrimHandle mesh);
        
        void create_device_local_buffer_with_data(
            const void* src_data,
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkBuffer& out_buffer,
            VkDeviceMemory& out_memory);


        std::unordered_map<MeshPrimHandle, MeshGPUData> mesh_map;
    };
}
