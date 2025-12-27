#include "vk_backend_buffer_mgr.h"

#include "vk_helpers.h"
#include "vk_macro.h"
#include "vk_render_backend.h"

void vk::BufferManager::bind_buffer_to_descriptor(RBDescriptorSetLayout layout, uint32_t binding, RBBufferHandle buffer)
{
    auto usage = buffer.get_usage_type();

    if (usage == ResourceUsageType::Frame)
    {
        for (uint32_t frame = 0; frame < vk::MAX_FRAMES_IN_FLIGHT; ++frame)
        {
            auto& buf = frames_ubos.at(buffer.get_identifier())[frame];

            VkDescriptorBufferInfo info{
                .buffer = buf.buffer,
                .offset = 0,
                .range  = VK_WHOLE_SIZE
            };

            VkWriteDescriptorSet write{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = frames_descriptors[frame].at(layout),
                .dstBinding = binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &info
            };

            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }
    else // Persistent
    {
        auto& buf = persistent_ubos.at(buffer.get_identifier());

        VkDescriptorBufferInfo info{
            .buffer = buf.buffer,
            .offset = 0,
            .range  = VK_WHOLE_SIZE
        };

        VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = persistent_descriptors.at(layout),
            .dstBinding = binding,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &info
        };

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

vk::BufferInfo& vk::BufferManager::get_buffer(RBBufferHandle buffer_handle, size_t frame_index)
{
    if (buffer_handle.get_usage_type() == ResourceUsageType::Frame)
    {
        size_t index = buffer_handle.get_identifier();
        return frames_ubos.at(index).at(frame_index);
    }
    return persistent_ubos[buffer_handle.get_identifier()];
}

void vk::BufferManager::allocate_descriptor_sets_for_layout(RBDescriptorSetLayout layout_handle, ResourceUsageType usage_type)
{
    auto& layout_data = descriptor_set_layouts.at(layout_handle);
    
    if (usage_type == ResourceUsageType::Frame)
    {
        for (size_t frame_index = 0; frame_index < vk::MAX_FRAMES_IN_FLIGHT; frame_index++)
        {
            VkDescriptorSetAllocateInfo alloc{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
            };
            alloc.descriptorPool = frame_pool;
            alloc.descriptorSetCount = 1;
            alloc.pSetLayouts = &layout_data.vk_layout;

            VkDescriptorSet set = VK_NULL_HANDLE;
            VK_CHECK(vkAllocateDescriptorSets(
                device,
                &alloc,
                &set
            ));
            frames_descriptors[frame_index].insert({layout_handle, set});
        }
    } else
    {
        VkDescriptorSetAllocateInfo alloc{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
        };
        alloc.descriptorPool = persistent_pool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &layout_data.vk_layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateDescriptorSets(
            device,
            &alloc,
            &set
        ));
        
        persistent_descriptors.insert({layout_handle, set});
    }

}

void vk::BufferManager::create_descriptor_pool()
{
    VkDescriptorPoolSize pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = vk::MAX_FRAMES_IN_FLIGHT * 16
        },
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = vk::MAX_FRAMES_IN_FLIGHT * 16
        }
    };

    VkDescriptorPoolCreateInfo pool_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
    };
    pool_info.poolSizeCount = uint32_t(std::size(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = vk::MAX_FRAMES_IN_FLIGHT * 16;
    pool_info.flags = 0;

    VK_CHECK(vkCreateDescriptorPool(
        device,
        &pool_info,
        nullptr,
        &frame_pool
    ));

    VK_CHECK(vkCreateDescriptorPool(
        device,
        &pool_info,
        nullptr,
        &persistent_pool
    ));
}

RBDescriptorSetLayout vk::BufferManager::allocate_descriptor_layout_handle()
{
    auto result = ++descriptor_set_counter;
    RBDescriptorSetLayout layout((uintptr_t)result);    
    return layout;
}

RBDescriptorSetLayout vk::BufferManager::create_descriptor_set_layout(
    const DescriptorSetLayoutDesc& descriptor_set_layout)
{
    std::vector<VkDescriptorSetLayoutBinding> vk_bindings;
    vk_bindings.reserve(descriptor_set_layout.bindings.size());

    for (const DescriptorBinding& b : descriptor_set_layout.bindings)
    {
        VkDescriptorSetLayoutBinding vk{};
        vk.binding = b.binding;
        vk.descriptorType = vk::to_vk_descriptor_type(b.type);
        vk.descriptorCount = b.count;
        vk.stageFlags = vk::to_vk_shader_stage_flags(b.stages);
        vk.pImmutableSamplers = nullptr;

        vk_bindings.push_back(vk);
    }

    VkDescriptorSetLayoutCreateInfo ci{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    ci.bindingCount = uint32_t(vk_bindings.size());
    ci.pBindings = vk_bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(
        device,
        &ci,
        nullptr,
        &layout
    ));
    DescriptorSetLayoutData layout_data {
        layout, 
        descriptor_set_layout.set_index
    };
    RBDescriptorSetLayout handle = allocate_descriptor_layout_handle();
    descriptor_set_layouts[handle] = layout_data;

    return handle;
}

vk::DescriptorSetLayoutData vk::BufferManager::get_vk_descriptor_set_layout(RBDescriptorSetLayout rb_handle)
{
    return descriptor_set_layouts[rb_handle];
}

RBDescriptorSet vk::BufferManager::get_descriptor_set(RBDescriptorSetLayout descriptor_set_layout,
    ResourceUsageType pool_type, uint32_t frame)
{
    if (pool_type == ResourceUsageType::Frame)
        return frames_descriptors[frame][descriptor_set_layout];
    return persistent_descriptors[descriptor_set_layout];
}

RBBufferHandle vk::BufferManager::create_uniform_buffer(size_t buffer_size, ResourceUsageType usage_type)
{
    if (usage_type == ResourceUsageType::Frame)
    {
        frames_ubo_counter++;
        
        const RBBufferHandle handle {frames_ubo_counter, ResourceUsageType::Frame};
        std::array<vk::BufferInfo, vk::MAX_FRAMES_IN_FLIGHT> buffers;
        for (int32_t index = 0; auto& _ : buffers)
        {
            vk::create_buffer(
                device,
                physical_device,
                buffer_size,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                buffers[index].buffer,
                buffers[index].memory);
            vkMapMemory(device, buffers[index].memory, 0, VK_WHOLE_SIZE, 0, &buffers[index].mapped_ptr);
            index++;
        }
        frames_ubos.emplace(handle.get_identifier(), buffers);
        
        return handle;
    } else
    {
        persistent_ubo_counter++;
        const RBBufferHandle handle {persistent_ubo_counter, ResourceUsageType::Persistent};
        
        vk::BufferInfo buffer_info;
        
        
        vk::create_buffer(
            device,
            physical_device,
            buffer_size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            buffer_info.buffer,
            buffer_info.memory);
        vkMapMemory(device, buffer_info.memory, 0, VK_WHOLE_SIZE, 0, &buffer_info.mapped_ptr);
        
        persistent_ubos[handle.get_identifier()] = buffer_info;
        return handle;
    }
}
