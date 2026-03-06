module vk:buffer_mgr;
import <vulkan/vulkan_core.h>;
import :helpers;
import :log;
import reflect;
#include "render_backends/vk/vk_macro.h"

RBDeviceAddress vk::BufferManager::get_buffer_device_address(RBBufferHandle buffer_handle, RBFrameHandle frame) const
{
    
    const auto& buf = get_buffer(buffer_handle, frame);

    return get_buffer_device_address(buf.buffer);
}

RBDeviceAddress vk::BufferManager::get_buffer_device_address(VkBuffer vk_buffer) const
{
    
    VkBufferDeviceAddressInfo info{
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO
    };
    info.buffer = vk_buffer;
    return vkGetBufferDeviceAddress(device, &info);
}

void vk::BufferManager::bind_buffer_to_descriptor(RBDescriptorSet set, uint32_t binding, RBBufferHandle buffer, RBFrameHandle frame)
{
    vk::BufferInfo* buf = nullptr;

    if (buffer.get_usage_type().is_frame_based())
    {
        buf = &frames_ubos
            .at(buffer.get_identifier())
            .at(frame);
    }
    else
    {
        buf = &persistent_ubos
            .at(buffer.get_identifier());
    }

    VkDescriptorBufferInfo info{
        .buffer = buf->buffer,
        .offset = 0,
        .range  = VK_WHOLE_SIZE
    };

    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = binding,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &info
    };

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

vk::BufferInfo& vk::BufferManager::get_buffer(RBBufferHandle buffer_handle, size_t frame_index)
{
    if (buffer_handle.get_usage_type().is_frame_based())
    {
        size_t index = buffer_handle.get_identifier();
        return frames_ubos.at(index).at(frame_index);
    }
    return persistent_ubos[buffer_handle.get_identifier()];
}

const vk::BufferInfo& vk::BufferManager::get_buffer(RBBufferHandle buffer_handle, size_t frame_index) const
{
    if (buffer_handle.get_usage_type().is_frame_based())
    {
        size_t index = buffer_handle.get_identifier();
        return frames_ubos.at(index).at(frame_index);
    }
    return persistent_ubos.at(buffer_handle.get_identifier());
}

std::vector<RBDescriptorSet> vk::BufferManager::allocate_descriptor_sets_for_layout(RBDescriptorSetLayout layout_handle, ResourceUsage usage_type, Name debug_name)
{
    auto& layout_data = descriptor_set_layouts.at(layout_handle);
    
    if (usage_type.is_frame_based())
    {
        std::vector<RBDescriptorSet> result;
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
            
            LogVkBufferManager.Log("allocate_descriptor_sets_for_layout. DescriptorSetLayout=%p, VkDescritorSet=%p (%s)",
                layout_data.vk_layout, set, debug_name.to_string().c_str());
            
            result.push_back(set);
        }
        return result;
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
        
        LogVkBufferManager.Log("allocate_descriptor_sets_for_layout. DescriptorSetLayout=%p, VkDescritorSet=%p (%s)",
            layout_handle, set, debug_name.to_string().c_str());
        
        return {set};
    }

}

void vk::BufferManager::create_descriptor_pool()
{
    VkDescriptorPoolSize pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1000 
        },
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1000 
        }
    };

    VkDescriptorPoolCreateInfo pool_info{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
    };
    pool_info.poolSizeCount = uint32_t(std::size(pool_sizes));
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = 1000;
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
    const DescriptorSetLayoutDesc& descriptor_set_layout, Name from_pass)
{
    std::vector<VkDescriptorSetLayoutBinding> vk_bindings;
    vk_bindings.reserve(descriptor_set_layout.bindings.size());

    for (const ResourceBinding& b : descriptor_set_layout.bindings)
    {
        VkDescriptorSetLayoutBinding vk{};
        vk.binding = *b.binding_index;
        vk.descriptorType = vk::to_vk_descriptor_type(b.parameter.type);
        vk.descriptorCount = 1; // b.count;
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
    
    
    LogVkBufferManager.Log("created descriptor set layout (debug_name=%s, set_index=%i), identifier: %p, num bindings: %i. DescriptorSetLayout: %p, from pass: %s",
        descriptor_set_layout.debug_name.to_string().c_str(), descriptor_set_layout.set_index, layout, vk_bindings.size(), handle, from_pass.to_string().c_str());
    for (const ResourceBinding& b : descriptor_set_layout.bindings)
    {
        LogVkBufferManager.Log("  * binding %i, type %s, name: %s, stages mask: %i ",
            b.binding_index, reflect::enum_name(b.parameter.type).to_string().c_str(), b.parameter.variable->to_string().c_str(), b.stages);
    }
    

    return handle;
}

vk::DescriptorSetLayoutData vk::BufferManager::get_vk_descriptor_set_layout(RBDescriptorSetLayout rb_handle)
{
    return descriptor_set_layouts.at(rb_handle);
}

RBBufferHandle vk::BufferManager::create_uniform_buffer(size_t buffer_size, ResourceUsage usage_type)
{
    if (usage_type.is_frame_based())
    {
        frames_ubo_counter++;
        
        const RBBufferHandle handle {frames_ubo_counter, usage_type};
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
        const RBBufferHandle handle {persistent_ubo_counter, usage_type};
        
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

void vk::BufferManager::update_uniform_buffer(RBBufferHandle buffer_handle, size_t size, void* data, RBFrameHandle frame)
{
    auto& buf = get_buffer(buffer_handle, frame);
    
    memcpy(buf.mapped_ptr, data, size);
    
    VkMappedMemoryRange range{};
    range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = buf.memory;
    range.offset = 0;
    range.size   = VK_WHOLE_SIZE;

    vkFlushMappedMemoryRanges(device, 1, &range);
}


void vk::BufferManager::create_device_local_buffer_with_data(
    const void* src_data,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer& out_buffer,
    VkDeviceMemory& out_memory,
    std::optional<RBCommandList> cmd_opt) const
{
    VkDevice device = this->device;
    VkPhysicalDevice phys = this->physical_device;

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
    }, [=]
    {
        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_memory, nullptr);
    }, cmd_opt);
    
    
}
