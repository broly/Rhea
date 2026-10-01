module;

#include <vulkan/vulkan_core.h>
#include <meshoptimizer.h>

module vk;

import :mesh_mgr;

import std.compat;
import assertions;
import fixed_string;
import profile;

import :helpers;
import :log;
import :device_extension_api;
#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"

DEFINE_LOGGER(LogVkMeshManager, Warning);


MeshTableInfo vk::MeshManager::get_mesh_table_info() const
{
    return {(void*)gpu_mesh_table.data(), gpu_mesh_table.size() };
}

GPUMesh vk::MeshManager::get_or_create_mesh_buffers(MeshPrimHandle handle, RTBuildMode rt_mode)
{
    auto it = mesh_map.find(handle);

    if (it != mesh_map.end())
    {
        if (rt_mode == RTBuildMode::build_blas &&
            it->second.blas == VK_NULL_HANDLE)
        {
            checkf(false, "not supported");
            // build_blas_for_existing(it->second);
        }

        auto& data = it->second;
        return gpu_mesh_table[data.mesh_table_index];
    }

    auto& primitive = handle.get();

    if (primitive.vertices.empty() ||
        primitive.indices.empty())
        {
            todo();
        }

    MeshGPUData data{};

    uint32_t mesh_index = gpu_mesh_table.size();
    data.index_count =
        static_cast<uint32_t>(primitive.indices.size());
    data.vertex_count = primitive.vertices.size();
    data.mesh_table_index = mesh_index;

    // triangles in the order that reuses the most transformed vertices (indexed draws, post transform cache)
    std::vector<uint32_t> indices;
    if (auto prepared = prepared_indices.find(handle); prepared != prepared_indices.end())
    {
        indices = std::move(prepared->second);
        prepared_indices.erase(prepared);
    }
    else
    {
        indices.resize(primitive.indices.size());
        meshopt_optimizeVertexCache(indices.data(), primitive.indices.data(), indices.size(), primitive.vertices.size());
    }

    const VkDeviceSize vertex_size = primitive.vertices.size() * sizeof(Vertex);
    const VkDeviceSize index_size = indices.size() * sizeof(uint32_t);

    // sub-allocated: one allocation per mesh ran into the allocation count limit (4096 on NVIDIA)
    const auto [vertex_block, vertex_offset] = suballocate(vertex_blocks, vertex_size, sizeof(Vertex), vertex_block_size);
    const auto [index_block, index_offset] = suballocate(index_blocks, index_size, sizeof(uint32_t), index_block_size);
    data.vertex_buffer = vertex_blocks[vertex_block].buffer;
    data.vertex_offset = vertex_offset;
    data.vertex_address = vertex_blocks[vertex_block].address + vertex_offset;
    data.index_buffer = index_blocks[index_block].buffer;
    data.index_offset = index_offset;
    data.index_address = index_blocks[index_block].address + index_offset;

    upload(data.vertex_buffer, vertex_offset, primitive.vertices.data(), vertex_size,
           data.index_buffer, index_offset, indices.data(), index_size);

    // ---- Optional BLAS ----
    if (rt_mode == RTBuildMode::build_blas)
        build_blas(data, data.vertex_address, data.index_address);

    mesh_map.emplace(handle, data);

    checkf(index_ranges.size() == mesh_index, "Index ranges out of step with the mesh table");
    index_ranges.push_back({
        .block = index_block,
        .first_index = uint32_t(index_offset / sizeof(uint32_t)),
        .index_count = data.index_count,
    });

    GPUMesh gpu{};
    gpu.vertex_address = data.vertex_address;
    gpu.index_address = data.index_address;
    gpu.index_count = data.index_count;
    gpu.mesh_index = mesh_index;

    gpu_mesh_table.push_back(gpu);

    mesh_table_dirty = true;

    return gpu;
}

void vk::MeshManager::prepare_mesh_buffers(std::span<const MeshPrimHandle> handles)
{
    prepared_indices.clear();

    // resolved here: the asset maps are not read from the workers
    std::vector<std::pair<MeshPrimHandle, const Primitive*>> jobs;
    for (const MeshPrimHandle& handle : handles)
    {
        if (mesh_map.contains(handle) || prepared_indices.contains(handle))
            continue;
        const Primitive& primitive = handle.get();
        if (primitive.vertices.empty() || primitive.indices.empty())
            continue;
        prepared_indices.emplace(handle, std::vector<uint32_t>(primitive.indices.size()));
        jobs.emplace_back(handle, &primitive);
    }
    if (jobs.empty())
        return;

    // biggest first: the last ones to start are short
    std::ranges::sort(jobs, std::greater{}, [] (const auto& job) { return job.second->indices.size(); });
    std::vector<std::vector<uint32_t>*> outputs;
    outputs.reserve(jobs.size());
    for (const auto& [handle, primitive] : jobs)
        outputs.push_back(&prepared_indices.at(handle));

    std::atomic<size_t> next_job = 0;
    auto work = [&]
    {
        for (size_t i = next_job++; i < jobs.size(); i = next_job++)
        {
            const Primitive& primitive = *jobs[i].second;
            meshopt_optimizeVertexCache(outputs[i]->data(), primitive.indices.data(), primitive.indices.size(),
                primitive.vertices.size());
        }
    };
    const size_t worker_count = std::min<size_t>(std::max(std::thread::hardware_concurrency(), 1u) - 1, jobs.size() - 1);
    {
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);
        for (size_t i = 0; i < worker_count; ++i)
            workers.emplace_back(work);
        work();
    }
}

std::pair<uint32_t, VkDeviceSize> vk::MeshManager::suballocate(std::vector<BufferBlock>& blocks, VkDeviceSize size,
    VkDeviceSize alignment, VkDeviceSize block_size)
{
    for (uint32_t index = 0; index < blocks.size(); ++index)
    {
        BufferBlock& block = blocks[index];
        const VkDeviceSize offset = (block.used + alignment - 1) / alignment * alignment;
        if (offset + size <= block.capacity)
        {
            block.used = offset + size;
            return { index, offset };
        }
    }

    BufferBlock& block = blocks.emplace_back();
    block.capacity = std::max(block_size, size);
    create_buffer(instance.device, instance.physical_device, block.capacity,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, block.buffer, block.memory);
    block.address = buffer_manager.get_buffer_device_address(block.buffer);
    block.used = size;
    return { uint32_t(blocks.size() - 1), 0 };
}

void vk::MeshManager::upload(VkBuffer vertex_buffer, VkDeviceSize vertex_offset, const void* vertices, VkDeviceSize vertex_size,
    VkBuffer index_buffer, VkDeviceSize index_offset, const void* indices, VkDeviceSize index_size)
{
    // into the shared upload batch: no submit and wait per primitive
    const UploadStaging staging = command_pool.allocate_upload(vertex_size + index_size);
    std::memcpy(staging.data, vertices, vertex_size);
    std::memcpy(staging.data + vertex_size, indices, index_size);

    command_pool.record_upload([&] (VkCommandBuffer cmd)
    {
        const VkBufferCopy vertex_copy{ .srcOffset = staging.offset, .dstOffset = vertex_offset, .size = vertex_size };
        vkCmdCopyBuffer(cmd, staging.buffer, vertex_buffer, 1, &vertex_copy);
        const VkBufferCopy index_copy{ .srcOffset = staging.offset + vertex_size, .dstOffset = index_offset, .size = index_size };
        vkCmdCopyBuffer(cmd, staging.buffer, index_buffer, 1, &index_copy);
        // read by the frames recorded after this (vertex pulling, index fetch, BLAS builds): flush_uploads ends the
        // batch with a barrier from the copies to every later command
    });
}

bool vk::MeshManager::update_vertices(MeshPrimHandle handle, std::span<const Vertex> vertices)
{
    auto it = mesh_map.find(handle);
    if (it == mesh_map.end())
        return false;
    const MeshGPUData& data = it->second;
    checkf(vertices.size() == data.vertex_count, "update_vertices: %zu vertices for a mesh of %u",
        vertices.size(), data.vertex_count);
    checkf(data.blas == VK_NULL_HANDLE, "update_vertices: the BLAS would need a rebuild");

    const VkDeviceSize size = vertices.size_bytes();
    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;
    vk::create_buffer(instance.device, instance.physical_device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_memory);
    vk::update_buffer(instance.device, staging_memory, vertices.data(), size);

    command_pool.submit([&] (VkCommandBuffer cmd)
    {
        VkBufferCopy copy{};
        copy.dstOffset = data.vertex_offset;
        copy.size = size;
        vkCmdCopyBuffer(cmd, staging_buffer, data.vertex_buffer, 1, &copy);

        // vertices are read through buffer device addresses by the vertex shaders
        VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr);
    });

    vk::destroy_buffer(instance.device, staging_buffer, staging_memory);
    return true;
}

void vk::MeshManager::build_blas(MeshGPUData& data, VkDeviceAddress vertex_address, VkDeviceAddress index_address)
{
    // --- Geometry description ---

    VkAccelerationStructureGeometryTrianglesDataKHR triangles{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR
    };
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertex_address;
    triangles.vertexStride = sizeof(Vertex);
    triangles.maxVertex = data.vertex_count - 1;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = index_address;

    VkAccelerationStructureGeometryKHR geometry{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR
    };
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles = triangles;

    VkAccelerationStructureBuildGeometryInfoKHR build_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR
    };
    build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build_info.geometryCount = 1;
    build_info.pGeometries = &geometry;
    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

    uint32_t primitive_count = data.index_count / 3;

    VkAccelerationStructureBuildSizesInfoKHR size_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };

    vk_ext::vkGetAccelerationStructureBuildSizesKHR(
        instance.device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &build_info,
        &primitive_count,
        &size_info
    );

    // --- Create BLAS buffer ---

    create_buffer(
        instance.device,
        instance.physical_device,
        size_info.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        data.blas_buffer,
        data.blas_memory
    );

    VkAccelerationStructureCreateInfoKHR as_create{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR
    };
    as_create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    as_create.size = size_info.accelerationStructureSize;
    as_create.buffer = data.blas_buffer;

    vk_ext::vkCreateAccelerationStructureKHR(
        instance.device,
        &as_create,
        nullptr,
        &data.blas
    );

    // --- Scratch buffer ---

    VkBuffer scratch_buffer;
    VkDeviceMemory scratch_memory;

    create_buffer(
        instance.device,
        instance.physical_device,
        size_info.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        scratch_buffer,
        scratch_memory
    );

    VkDeviceAddress scratch_address =
        buffer_manager.get_buffer_device_address(scratch_buffer);

    build_info.dstAccelerationStructure = data.blas;
    build_info.scratchData.deviceAddress = scratch_address;

    VkAccelerationStructureBuildRangeInfoKHR range_info{};
    range_info.primitiveCount = primitive_count;

    const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = {
        &range_info
    };

    // --- Build command ---
    
    command_pool.submit([&] (VkCommandBuffer cmd)
    {
        vk_ext::vkCmdBuildAccelerationStructuresKHR(
            cmd,
            1,
            &build_info,
            ranges
        );
    });

    // --- Get BLAS device address ---

    VkAccelerationStructureDeviceAddressInfoKHR address_info{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR
    };
    address_info.accelerationStructure = data.blas;

    data.blas_address = vk_ext::vkGetAccelerationStructureDeviceAddressKHR(
        instance.device,
        &address_info
    );

    // cleanup scratch
    vkDestroyBuffer(instance.device, scratch_buffer, nullptr);
    vkFreeMemory(instance.device, scratch_memory, nullptr);
}

void vk::MeshManager::bind_index_block(VkCommandBuffer cmd, uint32_t block) const
{
    checkf(block < index_blocks.size(), "No mesh index block %u", block);
    vkCmdBindIndexBuffer(cmd, index_blocks[block].buffer, 0, VK_INDEX_TYPE_UINT32);
}

void vk::MeshManager::bind(const RBCommandList& cmd, MeshPrimHandle mesh)
{
    auto it = mesh_map.find(mesh);
    checkf(it != mesh_map.end(), "Mesh not found");

    VkBuffer vb = it->second.vertex_buffer;
    VkBuffer ib = it->second.index_buffer;

    VkDeviceSize offsets[] = { it->second.vertex_offset };

    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, offsets);
    vkCmdBindIndexBuffer(cmd, ib, it->second.index_offset, VK_INDEX_TYPE_UINT32);
}



// ============================================================================
// Skinned meshes
// ============================================================================

static VkAccelerationStructureGeometryKHR make_triangles_geometry(
    VkDeviceAddress vertex_address, VkDeviceAddress index_address, uint32_t vertex_count)
{
    VkAccelerationStructureGeometryTrianglesDataKHR triangles{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR
    };
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertex_address;
    triangles.vertexStride = sizeof(Vertex);
    triangles.maxVertex = vertex_count - 1;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = index_address;

    VkAccelerationStructureGeometryKHR geometry{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR
    };
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles = triangles;
    return geometry;
}

static constexpr VkBuildAccelerationStructureFlagsKHR skinned_blas_flags =
    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
    VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

SkinnedMeshGPU vk::MeshManager::create_skinned_mesh(MeshPrimHandle source, const std::vector<SkinVertex>& skin, uint32_t bone_count, const PrimitiveMorphs& morphs, uint32_t morph_count, RTBuildMode rt_build_mode)
{
    PROFILE(__FUNCTION__);

    const Primitive& primitive = source.get();
    checkf(skin.size() == primitive.vertices.size(), "Skin data does not match vertex count");
    checkf(bone_count > 0, "Skinned mesh must have bones");

    // shared source geometry (bind pose vertices + indices), no BLAS needed
    get_or_create_mesh_buffers(source, RTBuildMode::none);
    const MeshGPUData& src = mesh_map.at(source);

    SkinnedMeshGPUData data{};
    data.source = source;
    data.vertex_count = src.vertex_count;
    data.index_count = src.index_count;
    data.bone_count = bone_count;

    // ---- skin weights ----
    buffer_manager.create_device_local_buffer_with_data(
        skin.data(),
        skin.size() * sizeof(SkinVertex),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        data.skin_buffer,
        data.skin_memory);

    // ---- morph targets ----
    if (!morphs.empty() && morph_count > 0)
    {
        checkf(morphs.offsets.size() == primitive.vertices.size() + 1, "Morph offsets do not match vertex count");

        data.morph_count = morph_count;
        buffer_manager.create_device_local_buffer_with_data(
            morphs.offsets.data(),
            morphs.offsets.size() * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            data.morph_offsets_buffer,
            data.morph_offsets_memory);
        buffer_manager.create_device_local_buffer_with_data(
            morphs.deltas.data(),
            morphs.deltas.size() * sizeof(MorphDelta),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            data.morph_deltas_buffer,
            data.morph_deltas_memory);
    }

    // ---- skinned output vertices, initialized with bind pose ----
    buffer_manager.create_device_local_buffer_with_data(
        primitive.vertices.data(),
        primitive.vertices.size() * sizeof(Vertex),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
        data.vertex_buffer,
        data.vertex_memory);

    // ---- bone matrices ring ----
    // weights are padded to 16 bytes: slots stay mat4 aligned
    data.bones_slot_size = sizeof(glm::mat4) * bone_count + ((sizeof(float) * data.morph_count + 15) & ~size_t(15));
    create_buffer(
        instance.device,
        instance.physical_device,
        data.bones_slot_size * kRenderMaxFramesInFlight,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        data.bones_buffer,
        data.bones_memory);
    vkMapMemory(instance.device, data.bones_memory, 0, VK_WHOLE_SIZE, 0, &data.bones_mapped);

    const VkDeviceAddress vertex_address = buffer_manager.get_buffer_device_address(data.vertex_buffer);
    const VkDeviceAddress index_address = src.index_address;

    // ---- BLAS (updatable) ----
    if (rt_build_mode == RTBuildMode::build_blas)
    {
        VkAccelerationStructureGeometryKHR geometry = make_triangles_geometry(vertex_address, index_address, data.vertex_count);

        VkAccelerationStructureBuildGeometryInfoKHR build_info{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR
        };
        build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_info.flags = skinned_blas_flags;
        build_info.geometryCount = 1;
        build_info.pGeometries = &geometry;
        build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

        uint32_t primitive_count = data.index_count / 3;

        VkAccelerationStructureBuildSizesInfoKHR size_info{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
        };
        vk_ext::vkGetAccelerationStructureBuildSizesKHR(
            instance.device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &build_info,
            &primitive_count,
            &size_info);

        create_buffer(
            instance.device,
            instance.physical_device,
            size_info.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            data.blas_buffer,
            data.blas_memory);

        VkAccelerationStructureCreateInfoKHR as_create{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR
        };
        as_create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        as_create.size = size_info.accelerationStructureSize;
        as_create.buffer = data.blas_buffer;
        vk_ext::vkCreateAccelerationStructureKHR(instance.device, &as_create, nullptr, &data.blas);

        // scratch is kept alive for refits
        const VkDeviceSize scratch_size = std::max(size_info.buildScratchSize, size_info.updateScratchSize);
        create_buffer(
            instance.device,
            instance.physical_device,
            scratch_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            data.blas_scratch_buffer,
            data.blas_scratch_memory);
        data.blas_scratch_address = buffer_manager.get_buffer_device_address(data.blas_scratch_buffer);

        build_info.dstAccelerationStructure = data.blas;
        build_info.scratchData.deviceAddress = data.blas_scratch_address;

        VkAccelerationStructureBuildRangeInfoKHR range_info{};
        range_info.primitiveCount = primitive_count;
        const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = { &range_info };

        command_pool.submit([&] (VkCommandBuffer cmd)
        {
            vk_ext::vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build_info, ranges);
        });

        VkAccelerationStructureDeviceAddressInfoKHR address_info{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR
        };
        address_info.accelerationStructure = data.blas;
        data.blas_address = vk_ext::vkGetAccelerationStructureDeviceAddressKHR(instance.device, &address_info);
    }

    // ---- mesh table entry: skinned vertices + shared indices ----
    data.mesh_table_index = (uint32_t)gpu_mesh_table.size();

    GPUMesh gpu{};
    gpu.vertex_address = vertex_address;
    gpu.index_address = index_address;
    gpu.index_count = data.index_count;
    gpu.mesh_index = data.mesh_table_index;
    gpu_mesh_table.push_back(gpu);
    // the skinned copy has the vertices of its source, in the same order: same indices
    index_ranges.push_back(index_ranges[src.mesh_table_index]);
    mesh_table_dirty = true;

    SkinnedMeshGPU& info = data.info;
    info.instance_id = (uint32_t)skinned_meshes.size();
    info.mesh_index = data.mesh_table_index;
    info.src_vertex_address = src.vertex_address;
    info.skin_address = buffer_manager.get_buffer_device_address(data.skin_buffer);
    info.dst_vertex_address = vertex_address;
    info.vertex_count = data.vertex_count;
    info.bone_count = bone_count;
    if (data.morph_count > 0)
    {
        info.morph_offsets_address = buffer_manager.get_buffer_device_address(data.morph_offsets_buffer);
        info.morph_deltas_address = buffer_manager.get_buffer_device_address(data.morph_deltas_buffer);
        info.morph_count = data.morph_count;
    }

    skinned_meshes.push_back(data);

    LogVkMeshManager.Log("Created skinned mesh instance %u (%u vertices, %u bones, %zu morph deltas)",
        info.instance_id, info.vertex_count, bone_count, data.morph_count > 0 ? morphs.deltas.size() : size_t(0));

    return info;
}

VkDeviceAddress vk::MeshManager::upload_bone_matrices(uint32_t instance_id, uint32_t frame, const std::vector<glm::mat4>& matrices, const std::vector<float>& morph_weights)
{
    SkinnedMeshGPUData& data = skinned_meshes.at(instance_id);
    checkf(matrices.size() == data.bone_count, "Bone count mismatch");
    checkf(frame < kRenderMaxFramesInFlight, "Invalid frame index");

    const VkDeviceSize offset = data.bones_slot_size * frame;
    uint8_t* slot = static_cast<uint8_t*>(data.bones_mapped) + offset;
    const size_t matrices_size = sizeof(glm::mat4) * data.bone_count;
    memcpy(slot, matrices.data(), matrices_size);

    if (data.morph_count > 0)
    {
        // missing weights (pose without morphs) are zero
        float* weights = reinterpret_cast<float*>(slot + matrices_size);
        const size_t provided = std::min<size_t>(morph_weights.size(), data.morph_count);
        std::fill(weights, weights + data.morph_count, 0.0f);
        if (provided > 0)
            memcpy(weights, morph_weights.data(), provided * sizeof(float));
    }

    // note: RBDeviceAddress must be unwrapped explicitly, `handle + offset` would go through operator bool
    const VkDeviceAddress base = buffer_manager.get_buffer_device_address(data.bones_buffer).handle;
    return base + offset;
}

void vk::MeshManager::cmd_refit_skinned_blas(VkCommandBuffer cmd, const std::vector<uint32_t>& instance_ids)
{
    if (instance_ids.empty())
        return;

    std::vector<VkAccelerationStructureGeometryKHR> geometries;
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> build_infos;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> range_ptrs;

    // reserve up front: build infos keep pointers into `geometries`
    geometries.reserve(instance_ids.size());
    build_infos.reserve(instance_ids.size());
    ranges.reserve(instance_ids.size());

    for (uint32_t instance_id : instance_ids)
    {
        const SkinnedMeshGPUData& data = skinned_meshes.at(instance_id);
        checkf(data.blas != VK_NULL_HANDLE, "Skinned mesh %u has no BLAS", instance_id);
        const MeshGPUData& src = mesh_map.at(data.source);

        geometries.push_back(make_triangles_geometry(
            data.info.dst_vertex_address,
            src.index_address,
            data.vertex_count));

        VkAccelerationStructureBuildGeometryInfoKHR build_info{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR
        };
        build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_info.flags = skinned_blas_flags;
        build_info.geometryCount = 1;
        build_info.pGeometries = &geometries.back();
        build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        build_info.srcAccelerationStructure = data.blas;
        build_info.dstAccelerationStructure = data.blas;
        build_info.scratchData.deviceAddress = data.blas_scratch_address;
        build_infos.push_back(build_info);

        VkAccelerationStructureBuildRangeInfoKHR range{};
        range.primitiveCount = data.index_count / 3;
        ranges.push_back(range);
    }

    for (auto& range : ranges)
        range_ptrs.push_back(&range);

    vk_ext::vkCmdBuildAccelerationStructuresKHR(
        cmd, (uint32_t)build_infos.size(), build_infos.data(), range_ptrs.data());

    VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        0,
        1, &barrier,
        0, nullptr,
        0, nullptr);
}
