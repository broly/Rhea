module vk:vertex_buffer_mgr;

import :helpers;
import <vulkan/vulkan_core.h>;
#include "render_backends/vk/vk_macro.h"

RBVertexBufferHandle vk::VertexBufferManager::create_vertex_buffer(const VertexBufferDesc& desc)
{
    VertexBufferInfo info{};
    info.per_frame_size = desc.frame_size;
    info.total_size = desc.frame_size * vk::MAX_FRAMES_IN_FLIGHT;
    info.dynamic = desc.dynamic;

    // -------------------------------------------------
    // 1️⃣ Create VkBuffer
    // -------------------------------------------------

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size  = info.total_size;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateBuffer(
        device,
        &bufferInfo,
        nullptr,
        &info.buffer));

    // -------------------------------------------------
    // 2️⃣ Memory requirements
    // -------------------------------------------------

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(device, info.buffer, &memReq);

    VkMemoryPropertyFlags properties;

    if (desc.dynamic)
    {
        properties =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    else
    {
        properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    uint32_t memoryTypeIndex =
        find_memory_type(physical_device, memReq.memoryTypeBits, properties);

    // -------------------------------------------------
    // 3️⃣ Allocate memory
    // -------------------------------------------------

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReq.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VK_CHECK(vkAllocateMemory(
        device,
        &allocInfo,
        nullptr,
        &info.memory));

    VK_CHECK(vkBindBufferMemory(
        device,
        info.buffer,
        info.memory,
        0));

    // -------------------------------------------------
    // 4️⃣ Persistent map (if dynamic)
    // -------------------------------------------------

    if (desc.dynamic)
    {
        VK_CHECK(vkMapMemory(
            device,
            info.memory,
            0,
            info.total_size,
            0,
            &info.mapped));
    }

    // -------------------------------------------------
    // 5️⃣ Store internally
    // -------------------------------------------------

    uint32_t id = (uint32_t)vertex_buffers.size();
    vertex_buffers.push_back(info);

    return { id };
}

void* vk::VertexBufferManager::get_mapped_pointer(RBVertexBufferHandle handle, RBFrameHandle frame)
{
    const VertexBufferInfo& info = vertex_buffers[handle.as<uint32_t>()];
    VkDeviceSize offset = frame * info.per_frame_size;
    uint8_t* memory = static_cast<uint8_t*>(vertex_buffers[handle.as<uint32_t>()].mapped);
    return memory + offset;
}

void vk::VertexBufferManager::bind_vertex_buffer(RBCommandList cmd, RBVertexBufferHandle handle, RBFrameHandle frame)
{
    const VertexBufferInfo& info = vertex_buffers[handle.as<uint32_t>()];

    VkDeviceSize offset = info.dynamic ? frame * info.per_frame_size : 0;
    
    vkCmdBindVertexBuffers(
        cmd,
        0,
        1,
        &info.buffer,
        &offset);
}
