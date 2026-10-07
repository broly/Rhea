module;

#include <vulkan/vulkan_core.h>

export module vk:mesh_mgr;

import std.compat;
import assertions;

import :instance;
import :immediate_commands;
import :buffer_mgr;
import assets;
import :mesh_gpu_data;
import render;
import glm;
import rhmath;
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
        // Forgets the buffers of a primitive (the next get_or_create_mesh_buffers makes new ones) and returns them
        // for free_mesh_buffers, once the frames that read them are done
        std::optional<MeshGPUData> take_mesh_buffers(MeshPrimHandle handle);
        void free_mesh_buffers(const MeshGPUData& data);
        // Destroys a skinned copy: its buffers, BLAS, mesh table entry and instance id are reused afterwards.
        // The frames that read it must be done
        void free_skinned_mesh(uint32_t instance_id);
        // RenderBackend::prepare_mesh_buffers: optimized indices of the primitives without buffers, on all cores
        void prepare_mesh_buffers(std::span<const MeshPrimHandle> handles);
        // same vertex count; the caller makes sure the GPU is idle. False when not uploaded.
        bool update_vertices(MeshPrimHandle handle, std::span<const Vertex> vertices);
        
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
        // freed skinned_meshes slots (free_skinned_mesh)
        std::vector<uint32_t> free_skinned_ids;

        const MeshGPUData& get_mesh_gpu_data(MeshPrimHandle handle)
        {
            auto data_it = mesh_map.find(handle);
            checkf(data_it != mesh_map.end(), "Could not find gpu mesh data");
            return data_it->second;
        }

        std::unordered_map<MeshPrimHandle, MeshGPUData> mesh_map;
        // prepare_mesh_buffers results, taken by get_or_create_mesh_buffers
        std::unordered_map<MeshPrimHandle, std::vector<uint32_t>> prepared_indices;

        // Vertices and indices of the meshes are sub-allocated from big blocks (one allocation per mesh ran into
        // the allocation count limit). Indexed mesh draws (RenderBackend::get_mesh_index_range) bind an index
        // block once for many meshes; the indices are reordered for the post transform vertex cache.
        struct BufferBlock
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkDeviceAddress address = 0;
            VkDeviceSize capacity = 0;
            VkDeviceSize used = 0;
            // freed ranges below `used`: offset -> size, coalesced
            std::map<VkDeviceSize, VkDeviceSize> free_ranges;
        };
        static constexpr VkDeviceSize vertex_block_size = 256ull << 20;   // bigger meshes get a block of their own
        static constexpr VkDeviceSize index_block_size = 64ull << 20;
        std::vector<BufferBlock> vertex_blocks;
        std::vector<BufferBlock> index_blocks;
        std::vector<MeshIndexRange> index_ranges;   // by mesh table index

        // block index, byte offset
        std::pair<uint32_t, VkDeviceSize> suballocate(std::vector<BufferBlock>& blocks, VkDeviceSize size,
            VkDeviceSize alignment, VkDeviceSize block_size);
        static void release_range(BufferBlock& block, VkDeviceSize offset, VkDeviceSize size);
        // recorded into the shared upload batch (ImmediateCommandPool::allocate_upload)
        void upload(VkBuffer vertex_buffer, VkDeviceSize vertex_offset, const void* vertices, VkDeviceSize vertex_size,
            VkBuffer index_buffer, VkDeviceSize index_offset, const void* indices, VkDeviceSize index_size);

        MeshIndexRange get_index_range(uint32_t mesh_index) const
        {
            checkf(mesh_index < index_ranges.size(), "No index range for mesh %u", mesh_index);
            return index_ranges[mesh_index];
        }
        void bind_index_block(VkCommandBuffer cmd, uint32_t block) const;

        std::vector<GPUMesh> gpu_mesh_table;
        // freed entries of the mesh table (and index_ranges), taken before the table grows: it is a fixed size buffer
        std::vector<uint32_t> free_mesh_table_slots;
        uint32_t add_mesh_table_entry(GPUMesh gpu, const MeshIndexRange& range);
        RBBufferHandle mesh_table_buffer;
        bool mesh_table_dirty = false;
    };
}
