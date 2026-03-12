module vk:mesh_mgr;

import <cassert>;
import <vulkan/vulkan_core.h>;
import :helpers;
#include "common/assertion_macros.h"
import :log;
import :device_extension_api;
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
    triangles.maxVertex = data.index_count; // todo: more precise
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
