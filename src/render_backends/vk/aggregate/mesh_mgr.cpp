module;

#include <vulkan/vulkan_core.h>

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

    VkDeviceSize vertex_size =
        primitive.vertices.size() * sizeof(Vertex);

    VkDeviceSize index_size =
        primitive.indices.size() * sizeof(uint32_t);
    
    VkBufferUsageFlags usage =
          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
        | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

    // ---- Vertex buffer ----
    buffer_manager.create_device_local_buffer_with_data(
        primitive.vertices.data(),
        vertex_size,
        usage,
        data.vertex_buffer,
        data.vertex_memory
    );

    // ---- Index buffer ----
    buffer_manager.create_device_local_buffer_with_data(
        primitive.indices.data(),
        index_size,
        usage,
        data.index_buffer,
        data.index_memory
    );

    // ---- Optional BLAS ----
    if (rt_mode == RTBuildMode::build_blas)
    {
        VkDeviceAddress vertex_address =
            buffer_manager.get_buffer_device_address(data.vertex_buffer);

        VkDeviceAddress index_address =
            buffer_manager.get_buffer_device_address(data.index_buffer);

        build_blas(data, vertex_address, index_address);
    }

    mesh_map.emplace(handle, std::move(data));
    
    GPUMesh gpu{};
    gpu.vertex_address = buffer_manager.get_buffer_device_address(data.vertex_buffer);
    gpu.index_address = buffer_manager.get_buffer_device_address(data.index_buffer);
    gpu.index_count = data.index_count;
    gpu.mesh_index = mesh_index;

    gpu_mesh_table.push_back(gpu);

    mesh_table_dirty = true;
    
    return gpu;
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

void vk::MeshManager::bind(const RBCommandList& cmd, MeshPrimHandle mesh)
{
    auto it = mesh_map.find(mesh);
    checkf(it != mesh_map.end(), "Mesh not found");

    VkBuffer vb = it->second.vertex_buffer;
    VkBuffer ib = it->second.index_buffer;

    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(cmd, 0, 1, &vb, offsets);
    vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT32);
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
    const VkDeviceAddress index_address = buffer_manager.get_buffer_device_address(src.index_buffer);

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
    mesh_table_dirty = true;

    SkinnedMeshGPU& info = data.info;
    info.instance_id = (uint32_t)skinned_meshes.size();
    info.mesh_index = data.mesh_table_index;
    info.src_vertex_address = buffer_manager.get_buffer_device_address(src.vertex_buffer);
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
            buffer_manager.get_buffer_device_address(src.index_buffer),
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
