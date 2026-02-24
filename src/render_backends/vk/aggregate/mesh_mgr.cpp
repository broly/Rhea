module vk:mesh_mgr;

import <cassert>;

import :helpers;
#include "common/assertion_macros.h"
import :log;
#include "logging/log_macro.h"
#include "profiling/profile.h"

DEFINE_LOGGER(LogVkMeshManager, Warning);

void vk::MeshManager::create_device_local_buffer_with_data(
    const void* src_data,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer& out_buffer,
    VkDeviceMemory& out_memory)
{
    VkDevice device = instance.get_device();
    VkPhysicalDevice phys = instance.get_physical_device();

    VkBuffer staging_buffer;
    VkDeviceMemory staging_memory;

    vk::create_buffer(
        device,
        phys,
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        staging_buffer,
        staging_memory
    );

    void* mapped;
    vkMapMemory(device, staging_memory, 0, size, 0, &mapped);
    memcpy(mapped, src_data, (size_t)size);
    vkUnmapMemory(device, staging_memory);

    vk::create_buffer(
        device,
        phys,
        size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        out_buffer,
        out_memory
    );

    command_pool.submit([=] (VkCommandBuffer cmd)
    {
        VkBufferCopy copy{};
        copy.size = size;
        vkCmdCopyBuffer(cmd, staging_buffer, out_buffer, 1, &copy);
    });

    vkDestroyBuffer(device, staging_buffer, nullptr);
    vkFreeMemory(device, staging_memory, nullptr);
    
}

void vk::MeshManager::get_or_create_mesh_buffers(MeshPrimHandle handle)
{
    if (mesh_map.contains(handle))
        return;

    auto& primitive = handle.get();

    if (primitive.vertices.empty() || primitive.indices.empty())
        return;

    MeshGPUData data{};
    data.index_count = static_cast<uint32_t>(primitive.indices.size());

    VkDeviceSize vertex_size =
        primitive.vertices.size() * sizeof(Vertex);

    VkDeviceSize index_size =
        primitive.indices.size() * sizeof(uint32_t);

    create_device_local_buffer_with_data(
        primitive.vertices.data(),
        vertex_size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        data.vertex_buffer,
        data.vertex_memory
    );

    create_device_local_buffer_with_data(
        primitive.indices.data(),
        index_size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        data.index_buffer,
        data.index_memory
    );

    mesh_map.emplace(handle, std::move(data));
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
