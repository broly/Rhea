module vk:mesh_mgr;

import <cassert>;

import :helpers;
#include "common/assertion_macros.h"

void vk::MeshManager::get_or_create_mesh_buffers(MeshPrimHandle handle)
{
    if (mesh_map.contains(handle))
        return;
    
    MeshGPUData data{};
    auto& primitive = handle.get();
    
    data.index_count = static_cast<uint32_t>(primitive.indices.size());
    
    vk::create_buffer(
        instance.get_device(),
        instance.get_physical_device(),
        primitive.vertices.size() * sizeof(Vertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        data.vertex_buffer,
        data.vertex_memory
    );
    
    void* mapped_data = nullptr;
    vkMapMemory(
        instance.get_device(),
        data.vertex_memory,
        0,
        primitive.vertices.size() * sizeof(Vertex),
        0,
        &mapped_data
    );
    memcpy(mapped_data, primitive.vertices.data(),
           primitive.vertices.size() * sizeof(Vertex));
    vkUnmapMemory(instance.get_device(), data.vertex_memory);
    
    vk::create_buffer(
        instance.get_device(),
        instance.get_physical_device(),
        primitive.indices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        data.index_buffer,
        data.index_memory
    );
    
    vkMapMemory(
        instance.get_device(),
        data.index_memory,
        0,
        primitive.indices.size() * sizeof(uint32_t),
        0,
        &mapped_data
    );
    memcpy(mapped_data, primitive.indices.data(),
           primitive.indices.size() * sizeof(uint32_t));
    vkUnmapMemory(instance.get_device(), data.index_memory);
    
    mesh_map[handle] = data;
}

void vk::MeshManager::bind(const RBCommandList& cmd, MeshPrimHandle mesh)
{
    auto mesh_info_it = mesh_map.find(mesh);
    
    checkf(mesh_info_it != mesh_map.end(), "Could not find mesh info for '%s' (geom: %i, prim: %i)",
        mesh.mesh.get().name.c_str(), mesh.geom_index, mesh.prim_index);
    
    static VkBuffer last_vb = nullptr;
    static VkBuffer last_ib = nullptr;

    
    VkBuffer vertex_buffers = mesh_info_it->second.vertex_buffer;
    VkBuffer index_buffer = mesh_info_it->second.index_buffer;
    
    if (vertex_buffers == last_vb &&
        index_buffer == last_ib)
    {
        return;
    }
    
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffers, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer, 0, VK_INDEX_TYPE_UINT32);
}
