export module vk:pipeline_manager;

import <vulkan/vulkan_core.h>;
import <map>;
import :render_resource;
import :pipeline;
import render;
import hash_utils;
import <type_traits>;

namespace vk
{
    struct PipelineLayoutInstanceData
    {
        PipelineLayoutDesc desc;
    };
    
    struct VkRenderResourceInfo
    {
        DescriptorSetLayoutDesc descritor_set_layout_desc;
        RBDescriptorSetLayout layout;
        frame_list<RBDescriptorSet> sets_per_frame;
        std::vector<frame_list<RBBufferHandle>> buffers;
    };

    export using UniqueResourcePair = std::tuple<const VkRenderResource*, uint32_t>;
}


namespace vk
{
    export class PipelineManager
    {
    public:
        PipelineManager(
            vk::Instance& in_instance,
            vk::SwapchainControl& in_swapchain,
            vk::ImageManager& in_image_manager, 
            vk::BufferManager& in_buffer_manager)
            : instance(in_instance)
            , swapchain(in_swapchain)
            , image_manager(in_image_manager)
            , buffer_manager(in_buffer_manager)
        {}
        
        PipelineObject* create_pipeline(const GraphicsPipelineDesc& desc);
        RBPipelineLayout create_pipeline_layout(const PipelineLayoutDesc& desc);
        
        VkDescriptorSetLayout get_empty_descriptor_set();
        
        std::shared_ptr<VkRenderResourceInstance> query_single_resource_instance(VkRenderResource* resource, 
            uint32_t unique_id, uint32_t instance_id, ResourceUsage usage);
        
        RBDescriptorSetLayout get_or_create_resource_descriptor_set_layout(VkRenderResource* resource);
        
        void push_constants(const RBCommandList& cmd, const void* data, size_t size, RBPipelineLayout pipeline_layout);
        void bind_descriptor_set(RBCommandList cmd, int set_index, RBDescriptorSet rb_descriptors,  Name debug_name);
        void bind_pipeline(RBCommandList cmd_list, PipelineObject* pipeline_object, VkRenderPass current_render_pass);
        
        void invalidate_pipeline_layout();
        
        inline const PipelineLayoutDesc& get_pipeline_layout_desc(RBPipelineLayout layout)
        {
            return instance_data.at(layout).desc;
        }
        
        void update_buffers();
        
        std::unordered_map<VkPipelineLayout, PipelineLayoutInstanceData> instance_data;
        
        vk::Instance& instance;
        vk::SwapchainControl& swapchain;
        vk::ImageManager& image_manager;
        vk::BufferManager& buffer_manager;
        
        RBPipelineLayout current_pipeline_layout = {};
        VkDescriptorSet current_descriptor_set = {};
        
        
        // currently working pipelines (moving from pending_pipelines)
        std::unordered_map<RBPipelineHandle, std::unique_ptr<VkPipelineObject>> pipelines;
    
    
        // pipelines objects that only have layouts, but not real pipelines yet
        std::vector<std::unique_ptr<VkPipelineObject>> pending_pipelines;
        
        
        std::map<
            UniqueResourcePair,
            std::vector<std::shared_ptr<VkRenderResourceInstance>> 
        > unique_resource_instances;
    
    
        struct ResourceInstanceData
        {
            std::vector<RBDescriptorSet> sets_per_frame = {};
            std::vector<std::vector<RBBufferHandle>> buffers = {}; // [binding][frame]
        };
    
        std::unordered_map<VkRenderResourceInstance*, ResourceInstanceData> resource_instance_data;

        std::unordered_map<const VkRenderResource*, VkRenderResourceInfo> resources_info;
        
        std::optional<VkDescriptorSetLayout> empty_descriptor_set;
        
        
    };
}

