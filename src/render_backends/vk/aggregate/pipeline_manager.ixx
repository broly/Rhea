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
        std::vector<VkDescriptorSetLayout> desc_set_layouts;
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
            vk::BufferManager& in_buffer_manager,
            vk::VkDebugObjectTracker& in_debug_object_tracker)
            : instance(in_instance)
            , swapchain(in_swapchain)
            , image_manager(in_image_manager)
            , buffer_manager(in_buffer_manager)
            , debug_object_tracker(in_debug_object_tracker)
        {}
        
        PipelineObject* create_graphics_pipeline(const PipelineCreateDesc_Graphics& desc, RBPipelineLayout pipeline_layout);
        PipelineObject* create_compute_pipeline(const PipelineCreateDesc_Compute& desc, RBPipelineLayout pipeline_layout);
        PipelineObject* create_raytrace_pipeline(const PipelineCreateDesc_RayTrace& desc, RBPipelineLayout pipeline_layout);
        
        RBPipelineLayout create_pipeline_layout(const PipelineLayoutDesc& desc);
        
        void destroy_pipeline(PipelineObject* pipeline);
        
        VkDescriptorSetLayout get_empty_descriptor_set();
        
        std::shared_ptr<VkRenderResourceInstance> query_single_resource_instance(VkRenderResource* resource, 
            uint32_t unique_id, uint32_t instance_id, ResourceUsage usage);
        
        RBDescriptorSetLayout get_or_create_resource_descriptor_set_layout(VkRenderResource* resource);
        
        void push_constants(const RBCommandList& cmd, const void* data, size_t size);
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
        vk::VkDebugObjectTracker& debug_object_tracker;
        
        RBPipelineLayout current_pipeline_layout = {};

        // ----------------------------------------------------------------
        // Per-(bind_point, set_index) descriptor set caching.
        //
        // Vulkan keeps INDEPENDENT bind tables for VK_PIPELINE_BIND_POINT_
        // GRAPHICS, _COMPUTE, _RAY_TRACING_KHR. Binding a descriptor set
        // for compute does NOT affect the graphics bind table, and vice
        // versa. The early-out for "skip rebinding the same descriptor set"
        // therefore must be keyed by (bind_point, set_index), not by a
        // single global slot.
        //
        // Old code used a single `VkDescriptorSet current_descriptor_set`
        // globally. Symptom: a graphics pass binds set 6 = X. Then a
        // compute pass binds set 6 = X. Old code sees X == X and skips
        // the rebind — but the compute bind table actually has nothing in
        // slot 6, so the compute shader reads UNDEFINED descriptor data.
        // For the NN denoiser this manifested as activations [3..N] being
        // black: the input pass thought it had set 6 bound while the
        // compute pipeline didn't, so its writes went into whatever
        // (uninitialised / different) descriptor was actually in compute
        // slot 6.
        // ----------------------------------------------------------------
        struct BindKey
        {
            VkPipelineBindPoint bind_point;
            uint32_t            set_index;
            bool operator==(const BindKey& o) const noexcept
            {
                return bind_point == o.bind_point && set_index == o.set_index;
            }
        };
        struct BindKeyHash
        {
            size_t operator()(const BindKey& k) const noexcept
            {
                return std::hash<uint32_t>{}(uint32_t(k.bind_point)) * 0x9E3779B1u
                     ^ std::hash<uint32_t>{}(k.set_index);
            }
        };
        std::unordered_map<BindKey, VkDescriptorSet, BindKeyHash> current_descriptor_sets;

        // Last bound pipeline layout per bind point, used to invalidate
        // current_descriptor_sets when a layout change disturbs the bind
        // table. See bind_pipeline() for the reasoning.
        std::unordered_map<VkPipelineBindPoint, RBPipelineLayout> last_layout_per_bind_point;

        VkPipelineObject* current_pipeline_object = nullptr;
        
        
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

