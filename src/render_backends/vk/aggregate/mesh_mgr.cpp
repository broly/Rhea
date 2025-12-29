module vk:mesh_mgr;

import :helpers;

void vk::MeshManager::get_or_create_mesh_buffers(MeshHandle handle)
{
    if (mesh_map.contains(handle))
        return;
    
    MeshGPUData data{};
    auto& mesh = handle.get();

    data.index_count = static_cast<uint32_t>(mesh.indices.size());

    vk::create_buffer(
        instance.get_device(),
        instance.get_physical_device(),
        mesh.vertices.size() * sizeof(Vertex),
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
        mesh.vertices.size() * sizeof(Vertex),
        0,
        &mapped_data
    );
    memcpy(mapped_data, mesh.vertices.data(),
           mesh.vertices.size() * sizeof(Vertex));
    vkUnmapMemory(instance.get_device(), data.vertex_memory);

    vk::create_buffer(
        instance.get_device(),
        instance.get_physical_device(),
        mesh.indices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        data.index_buffer,
        data.index_memory
    );

    vkMapMemory(
        instance.get_device(),
        data.index_memory,
        0,
        mesh.indices.size() * sizeof(uint32_t),
        0,
        &mapped_data
    );
    memcpy(mapped_data, mesh.indices.data(),
           mesh.indices.size() * sizeof(uint32_t));
    vkUnmapMemory(instance.get_device(), data.index_memory);

    mesh_map[handle] = data;
}

void vk::MeshManager::bind(const RBCommandList& cmd, MeshHandle mesh)
{
    VkBuffer vertex_buffers = mesh_map[mesh].vertex_buffer;
    VkBuffer index_buffer = mesh_map[mesh].index_buffer;
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffers, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer, 0, VK_INDEX_TYPE_UINT32);
}
