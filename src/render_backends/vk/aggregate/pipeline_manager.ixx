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
    
    struct VkRenderResourcePipelineInfo
    {
        DescriptorSetLayoutDesc descritor_set_layout_desc;
        RBDescriptorSetLayout layout;
        frame_list<RBDescriptorSet> sets_per_frame;
        std::vector<frame_list<RBBufferHandle>> buffers;
    };
    
    struct UniqueResourcePair
    {
        const VkRenderResource* resource;
        RBPipelineLayout pipeline_layout;
        
        bool operator==(const UniqueResourcePair& other) const
        {
            return resource == other.resource && pipeline_layout == other.pipeline_layout;
        }
    };

    export using UniqueResourceTrio = std::tuple<const VkRenderResource*, RBPipelineLayout, uint32_t>;
}

export template<>
struct std::hash<vk::UniqueResourcePair>
{
    size_t operator()(const vk::UniqueResourcePair& h) const noexcept
    {
        size_t seed = 0;
        hash_combine(seed, (size_t)h.resource);
        hash_combine(seed, (size_t)h.pipeline_layout);
        return seed;
    }
};
    

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
        RBPipelineLayout create_layout(const PipelineLayoutDesc& desc);
        
        VkDescriptorSetLayout get_empty_descriptor_set();
        
        std::shared_ptr<VkRenderResourceInstance> query_single_resource_instance(VkRenderResource* resource, RBPipelineLayout pipeline_layout, uint32_t unique_id, uint32_t instance_id, ResourceUsage
            usage);
        
        void push_constants(const RBCommandList& cmd, const void* data, size_t size, RBPipelineLayout pipeline_layout);
        void bind_descriptor_set(RBCommandList cmd, int set_index, RBDescriptorSet rb_descriptors, 
            RBPipelineLayout pipeline_layout, Name debug_name);
        void bind_pipeline(RBCommandList cmd_list, PipelineObject* pipeline_object, VkRenderPass current_render_pass);
        
        const PipelineLayoutDesc& get_pipeline_layout_desc(RBPipelineLayout layout);
        
        void update_buffers();
        
        std::map<VkPipelineLayout, PipelineLayoutInstanceData> instance_data;
        
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
            UniqueResourceTrio,
            std::vector<std::shared_ptr<VkRenderResourceInstance>> 
        > unique_resource_instances;
    
    
        struct ResorceInstancePipelineData
        {
            std::vector<RBDescriptorSet> sets_per_frame = {};
            std::vector<std::vector<RBBufferHandle>> buffers = {}; // [binding][frame]
        };
    
        std::unordered_map<VkRenderResourceInstance*, ResorceInstancePipelineData> instance_pipeline_data;

        std::unordered_map<UniqueResourcePair, VkRenderResourcePipelineInfo> resources_pipeline_info;
        
        std::optional<VkDescriptorSetLayout> empty_descriptor_set;
    };
}

