module vk:immediate_commands;

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
}

void vk::ImmediateCommandPool::submit(std::function<void(VkCommandBuffer)>&& fn)
{
    vkResetFences(instance.device, 1, &fence);
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    fn(cmd);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    vkQueueSubmit(instance.graphics_queue, 1, &submit, fence);
    vkWaitForFences(instance.device, 1, &fence, VK_TRUE, UINT64_MAX);
}
