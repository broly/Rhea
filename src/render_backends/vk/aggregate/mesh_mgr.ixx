export module vk:mesh_mgr;

import :instance;
import :immediate_commands;
import :buffer_mgr;
import assets;
import <unordered_map>;
import <vulkan/vulkan_core.h>;
import :mesh_gpu_data;
#include "common/assertion_macros.h"


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
        
        
        MeshTableInfo get_mesh_table_info() const;
        
        
        GPUMesh get_or_create_mesh_buffers(MeshPrimHandle handle, RTBuildMode rt_build_mode);
        
        void build_blas(
            MeshGPUData& data,
            VkDeviceAddress vertex_address,
            VkDeviceAddress index_address);
        
        void bind(const RBCommandList& cmd, MeshPrimHandle mesh);
        
        const MeshGPUData& get_mesh_gpu_data(MeshPrimHandle handle)
        {
            auto data_it = mesh_map.find(handle);
            checkf(data_it != mesh_map.end(), "Could not find gpu mesh data");
            return data_it->second;
        }

        std::unordered_map<MeshPrimHandle, MeshGPUData> mesh_map;
        
        std::vector<GPUMesh> gpu_mesh_table;
        RBBufferHandle mesh_table_buffer;
        bool mesh_table_dirty = false;
    };
}
