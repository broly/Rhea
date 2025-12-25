#pragma once
#include <cstdint>
#include <map>
#include <vulkan/vulkan_core.h>

#include "vk_context.h"
#include "vk_internal_types.h"
#include "render/graphics_pipeline.h"

class VkRenderBackend;

namespace vk
{
    
    struct BufferInfo
    {
        VkBuffer buffer;
        VkDeviceMemory memory;
        void* mapped_ptr;
    };
    
    class ResourceManager
    {
    public:
        ResourceManager(VkRenderBackend& in_render_backend);
    
        uint32_t persistent_ubo_counter = 0;
        std::map<uint32_t, BufferInfo> persistent_ubos;
        
        uint32_t frames_ubo_counter = 0;
        std::map<uint32_t, std::array<BufferInfo, MAX_FRAMES_IN_FLIGHT>> frames_ubos;
        
        VkDescriptorPool frame_pool = VK_NULL_HANDLE;
        VkDescriptorPool persistent_pool = VK_NULL_HANDLE;
        
        VkRenderBackend& render_backend;
        
        std::array<
            std::map<RBDescriptorSetLayout, RBDescriptorSet>,
            MAX_FRAMES_IN_FLIGHT>
        frames_descriptors;
        
        std::map<RBDescriptorSetLayout, DescriptorSetLayoutData> descriptor_set_layouts;
        
        std::map<RBDescriptorSetLayout, RBDescriptorSet> persistent_descriptors;
        uint64_t descriptor_set_counter = 0;
        
        void bind_buffer_to_descriptor(RBDescriptorSetLayout layout, uint32_t binding, RBBufferHandle buffer);
        vk::BufferInfo& get_buffer(RBBufferHandle buffer_handle, size_t frame_index);
        void allocate_descriptor_sets_for_layout(RBDescriptorSetLayout layout_handle, ResourceUsageType usage_type);
        void create_descriptor_pool();
        RBDescriptorSetLayout allocate_descriptor_layout_handle();
        RBDescriptorSetLayout create_descriptor_set_layout(const DescriptorSetLayoutDesc& descriptor_set_layout);
        DescriptorSetLayoutData get_vk_descriptor_set_layout(RBDescriptorSetLayout rb_handle);
        RBDescriptorSet get_descriptor_set(
            RBDescriptorSetLayout descriptor_set_layout, 
            ResourceUsageType resource_usage,
            uint32_t frame);
        RBBufferHandle create_uniform_buffer(size_t size, ResourceUsageType resource_usage);
    };
}
