export module vk:mesh_mgr;

import :instance;
import :immediate_commands;
import :buffer_mgr;
import assets;
import <unordered_map>;
import <vulkan/vulkan_core.h>;
import :mesh_gpu_data;
import render;
import glm;
#include "common/assertion_macros.h"


namespace vk
{
    struct SkinnedMeshGPUData
    {
        MeshPrimHandle source;
        
        VkBuffer skin_buffer = VK_NULL_HANDLE;
        VkDeviceMemory skin_memory = VK_NULL_HANDLE;
        
        // skinned output vertices (same layout as Vertex)
        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
        
        // sparse morph deltas (CSR by vertex), null when the primitive has no morphs
        VkBuffer morph_offsets_buffer = VK_NULL_HANDLE;
        VkDeviceMemory morph_offsets_memory = VK_NULL_HANDLE;
        VkBuffer morph_deltas_buffer = VK_NULL_HANDLE;
        VkDeviceMemory morph_deltas_memory = VK_NULL_HANDLE;

        // bone matrices ring: one slot per frame in flight (host visible, persistently mapped).
        // A slot holds the bone matrices followed by the morph target weights.
        VkBuffer bones_buffer = VK_NULL_HANDLE;
        VkDeviceMemory bones_memory = VK_NULL_HANDLE;
        void* bones_mapped = nullptr;
        VkDeviceSize bones_slot_size = 0;
        
        // BLAS over skinned vertices (built with ALLOW_UPDATE for refits)
        VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
        VkBuffer blas_buffer = VK_NULL_HANDLE;
        VkDeviceMemory blas_memory = VK_NULL_HANDLE;
        VkDeviceAddress blas_address = 0;
        VkBuffer blas_scratch_buffer = VK_NULL_HANDLE;
        VkDeviceMemory blas_scratch_memory = VK_NULL_HANDLE;
        VkDeviceAddress blas_scratch_address = 0;
        
        uint32_t vertex_count = 0;
        uint32_t index_count = 0;
        uint32_t bone_count = 0;
        uint32_t morph_count = 0;
        uint32_t mesh_table_index = 0;
        
        SkinnedMeshGPU info;
    };
    
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
        
        SkinnedMeshGPU create_skinned_mesh(MeshPrimHandle source, const std::vector<SkinVertex>& skin, uint32_t bone_count, const PrimitiveMorphs& morphs, uint32_t morph_count, RTBuildMode rt_build_mode);
        
        VkDeviceAddress upload_bone_matrices(uint32_t instance_id, uint32_t frame, const std::vector<glm::mat4>& matrices, const std::vector<float>& morph_weights);
        
        void cmd_refit_skinned_blas(VkCommandBuffer cmd, const std::vector<uint32_t>& instance_ids);
        
        const SkinnedMeshGPUData& get_skinned_mesh(uint32_t instance_id) const
        {
            checkf(instance_id < skinned_meshes.size(), "Invalid skinned mesh instance");
            return skinned_meshes[instance_id];
        }
        
        std::vector<SkinnedMeshGPUData> skinned_meshes;
        
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
