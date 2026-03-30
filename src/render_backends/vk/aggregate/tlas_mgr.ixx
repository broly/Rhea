export module vk:tlas_mgr;

import <vulkan/vulkan_core.h>;
import :instance;
import :immediate_commands;
import :buffer_mgr;
import :mesh_gpu_data;
import :mesh_mgr;

struct TLASData
{
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;

    VkDeviceAddress address = 0;

    VkBuffer instance_buffer = VK_NULL_HANDLE;
    VkDeviceMemory instance_memory = VK_NULL_HANDLE;
};

struct RTInstance
{
    VkTransformMatrixKHR transform;
    uint32_t instanceCustomIndex : 24;
    uint32_t mask : 8;

    uint32_t instanceShaderBindingTableRecordOffset : 24;
    uint32_t flags : 8;

    uint64_t accelerationStructureReference;
};


namespace vk
{
    class TLASManager
    {
    public:
        TLASManager(vk::Instance& in_instance, vk::ImmediateCommandPool& in_command_pool, vk::BufferManager& in_buffer_mgr,
            vk::MeshManager& in_mesh_manager)
                : instance(in_instance)
                , command_pool(in_command_pool)
                , buffer_manager(in_buffer_mgr)
                , mesh_manager(in_mesh_manager)
        {}
        vk::Instance& instance;
        vk::ImmediateCommandPool& command_pool;
        vk::BufferManager& buffer_manager;
        vk::MeshManager& mesh_manager;
        
        TLASData tlas;

        RBAccelStruct build_tlas(
            RBCommandList cmd, const std::vector<TLASInfo>& objects
        );
    };
}