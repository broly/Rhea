module;

#include <vulkan/vulkan_core.h>

module vk;

import :immediate_commands;
import :helpers;

import std.compat;
import assertions;

#include "common/assertion_macros.h"
#include "render_backends/vk/vk_macro.h"

void vk::ImmediateCommandPool::init()
{
    VkCommandPoolCreateInfo cpci{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
    };
    cpci.queueFamilyIndex = instance.queues.graphics;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(
        instance.get_device(),
        &cpci,
        nullptr,
        &pool
    ));

    VkCommandBufferAllocateInfo cbai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
    };
    cbai.commandPool = pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;

    VK_CHECK(vkAllocateCommandBuffers(
        instance.get_device(),
        &cbai,
        &cmd
    ));

    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VK_CHECK(vkCreateFence(
        instance.get_device(),
        &fci,
        nullptr,
        &fence
    ));

    for (UploadBatch& batch : upload_batches)
    {
        VK_CHECK(vkAllocateCommandBuffers(instance.device, &cbai, &batch.cmd));
        VK_CHECK(vkCreateFence(instance.device, &fci, nullptr, &batch.fence));
        vk::create_buffer(instance.device, instance.physical_device, upload_batch_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, batch.staging, batch.staging_memory);
        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(instance.device, batch.staging_memory, 0, upload_batch_size, 0, &mapped));
        batch.mapped = static_cast<std::byte*>(mapped);
    }
}

vk::ImmediateCommandPool::UploadBatch& vk::ImmediateCommandPool::open_upload_batch()
{
    UploadBatch& batch = upload_batches[current_upload_batch];
    if (batch.recording)
        return batch;

    if (batch.in_flight)
    {
        VK_CHECK(vkWaitForFences(instance.device, 1, &batch.fence, VK_TRUE, UINT64_MAX));
        batch.in_flight = false;
    }
    for (auto [buffer, memory] : batch.dedicated)
        vk::destroy_buffer(instance.device, buffer, memory);
    batch.dedicated.clear();

    VK_CHECK(vkResetFences(instance.device, 1, &batch.fence));
    VK_CHECK(vkResetCommandBuffer(batch.cmd, 0));
    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(batch.cmd, &begin));
    batch.used = 0;
    batch.recording = true;
    return batch;
}

vk::UploadStaging vk::ImmediateCommandPool::allocate_upload(VkDeviceSize size, VkDeviceSize alignment)
{
    UploadBatch* batch = &open_upload_batch();

    if (size > upload_batch_size)
    {
        UploadStaging staging;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        vk::create_buffer(instance.device, instance.physical_device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging.buffer, memory);
        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(instance.device, memory, 0, size, 0, &mapped));
        staging.data = static_cast<std::byte*>(mapped);
        batch->dedicated.emplace_back(staging.buffer, memory);
        return staging;
    }

    VkDeviceSize offset = (batch->used + alignment - 1) / alignment * alignment;
    if (offset + size > upload_batch_size)
    {
        flush_uploads();
        batch = &open_upload_batch();
        offset = 0;
    }
    batch->used = offset + size;
    return { batch->staging, offset, batch->mapped + offset };
}

void vk::ImmediateCommandPool::record_upload(const std::function<void(VkCommandBuffer)>& commands)
{
    commands(open_upload_batch().cmd);
}

void vk::ImmediateCommandPool::flush_uploads()
{
    UploadBatch& batch = upload_batches[current_upload_batch];
    if (!batch.recording)
        return;

    // buffer copies (meshes) become visible to every later command; images were transitioned by their uploads
    VkMemoryBarrier barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(batch.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

    VK_CHECK(vkEndCommandBuffer(batch.cmd));
    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &batch.cmd;
    VK_CHECK(vkQueueSubmit(instance.graphics_queue, 1, &submit, batch.fence));
    batch.recording = false;
    batch.in_flight = true;
    current_upload_batch = (current_upload_batch + 1) % upload_batch_count;
}

void vk::ImmediateCommandPool::submit(std::function<void(VkCommandBuffer)>&& commands)
{
    // the commands may read what the open upload batch writes
    flush_uploads();

    vkResetFences(instance.device, 1, &fence);
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    commands(cmd);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(instance.graphics_queue, 1, &submit, fence));
    VK_CHECK(vkWaitForFences(instance.device, 1, &fence, VK_TRUE, UINT64_MAX));
}

void vk::ImmediateCommandPool::submit(std::function<void(VkCommandBuffer)>&& commands, std::optional<RBCommandList> cmd)
{
    if (cmd.has_value())
    {
        commands(cmd.value());
    }
    else
    {
        submit(std::move(commands));
    }
}

void vk::ImmediateCommandPool::submit(std::function<void(VkCommandBuffer)>&& commands, std::function<void()> finally)
{
    submit(std::move(commands));
    finally();
}

void vk::ImmediateCommandPool::submit(std::function<void(VkCommandBuffer)>&& commands, std::function<void()> finally,
    std::optional<RBCommandList> cmd)
{
    if (cmd.has_value())
    {
        commands(cmd.value());
        add_release_callback(cmd.value(), std::move(finally));
    }
    else
    {
        submit(std::move(commands), std::move(finally));
    }
}

void vk::ImmediateCommandPool::add_release_callback(RBCommandList in_cmd, std::function<void()>&& callback)
{
    release_callbacks[in_cmd].push_back(std::move(callback));
}

void vk::ImmediateCommandPool::on_begin_main_commands(RBCommandList cmd)
{
    
}

void vk::ImmediateCommandPool::on_end_main_commands(RBCommandList cmd)
{
    
}

VkCommandBuffer vk::ImmediateCommandPool::begin_single_time_commands()
{
    checkf(cmd == VK_NULL_HANDLE, "Commands already started");
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(instance.device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void vk::ImmediateCommandPool::end_single_time_commands()
{
    vkEndCommandBuffer(cmd);
    flush_uploads();

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(instance.graphics_queue, 1, &submit, VK_NULL_HANDLE));
    vkQueueWaitIdle(instance.graphics_queue);

    vkFreeCommandBuffers(instance.device, pool, 1, &cmd);
    
    
    cmd = VK_NULL_HANDLE;
    
    
}

void vk::ImmediateCommandPool::flush_garbage(RBFrameHandle frame)
{
    
    
    for (auto& [_, callbacks] : release_callbacks)
    {
        for (auto& callback : callbacks)
        {
            callback();
        }
    }
    release_callbacks = {};
    
    active_main_command_buffer = {};
}
