export module vk:buffer_mgr;
import <cstdint>;
import <map>;
import <vulkan/vulkan_core.h>;

import :context;
import :internal_types;
import :swapchain_control;
import render;
import <optional>;

class VkRenderBackend;

namespace vk
{
    
    struct BufferInfo
    {
        VkBuffer buffer;
        VkDeviceMemory memory;
        void* mapped_ptr = nullptr;
    };
    
    class BufferManager
    {
    public:
        BufferManager(const VkDevice& in_device, const VkPhysicalDevice& in_physical_device, 
            const vk::SwapchainControl& in_swapchain, vk::ImmediateCommandPool& in_command_pool)
            : device(in_device)
            , physical_device(in_physical_device)
            , swapchain(in_swapchain)
            , command_pool(in_command_pool)
        {}
    
        uint32_t persistent_ubo_counter = 0;
        std::map<uint32_t, BufferInfo> persistent_ubos;
        
        uint32_t frames_ubo_counter = 0;
        std::map<uint32_t, std::array<BufferInfo, MAX_FRAMES_IN_FLIGHT>> frames_ubos;
        
        VkDescriptorPool frame_pool = VK_NULL_HANDLE;
        VkDescriptorPool persistent_pool = VK_NULL_HANDLE;
        
        RBDeviceAddress get_buffer_device_address(RBBufferHandle buffer_handle, RBFrameHandle frame = 0) const;
        RBDeviceAddress get_buffer_device_address(VkBuffer vk_buffer) const;
        
        
        std::map<RBDescriptorSetLayout, DescriptorSetLayoutData> descriptor_set_layouts;
        
        std::map<RBDescriptorSet, RBDescriptorSetLayout> descriptor_sets_validation;

        std::vector<RBDescriptorSet> descriptors;
        uint64_t descriptor_set_counter = 0;
        
        void bind_buffer_to_descriptor(RBDescriptorSet set, uint32_t binding, RBBufferHandle buffer, RBFrameHandle frame, VkDescriptorType descriptor_type);
        vk::BufferInfo& get_buffer(RBBufferHandle buffer_handle, size_t frame_index);
        const vk::BufferInfo& get_buffer(RBBufferHandle buffer_handle, size_t frame_index) const;
        std::vector<RBDescriptorSet> allocate_descriptor_sets_for_layout(RBDescriptorSetLayout layout_handle, ResourceUsage usage_type, Name debug_name);
        void create_descriptor_pool();
        RBDescriptorSetLayout allocate_descriptor_layout_handle();
        RBDescriptorSetLayout create_descriptor_set_layout(const DescriptorSetLayoutDesc& descriptor_set_layout, Name resource_name);
        DescriptorSetLayoutData get_vk_descriptor_set_layout(RBDescriptorSetLayout rb_handle);
        RBBufferHandle create_uniform_buffer(size_t size, ResourceUsage resource_usage);
        RBBufferHandle create_storage_buffer(size_t buffer_size, ResourceUsage usage_type, bool host_visible);
        
        void update_any_buffer(RBBufferHandle buffer_handle, size_t size, void* data, RBFrameHandle frame);

        
        void create_device_local_buffer_with_data(
            const void* src_data,
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkBuffer& out_buffer,
            VkDeviceMemory& out_memory,
            std::optional<RBCommandList> cmd_opt = std::nullopt) const;
        
        void update_sampled_image(RBDescriptorSet set, uint32_t binding, VkImageView image,
                                           ResourceUsage usage, VkSampler sampler, uint32_t array_index = 0);
        
    private:
        const VkDevice& device;
        const VkPhysicalDevice& physical_device;
        
        const vk::SwapchainControl& swapchain;
        vk::ImmediateCommandPool& command_pool;
    };
    
}
