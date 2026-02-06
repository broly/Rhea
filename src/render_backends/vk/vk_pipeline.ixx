export module vk:pipeline;

import <vulkan/vulkan_core.h>;

import :context;
import :shader;
import :instance;
import :swapchain_control;
import :buffer_mgr;
import render;
import <unordered_map>;
import <optional>;
import <cassert>;
import :render_resource;
import :render_resource_instance;
import :reflection;

using UniqueResourcePair = std::pair<VkRenderResource*, uint32_t>;

class VkPipelineObject : public PipelineObject
{
public:
    VkPipelineObject(
        vk::Instance& in_instance,
        vk::SwapchainControl& in_swapchain,
        vk::BufferManager& in_buffer_manager,
        const GraphicsPipelineDesc& desc);
    ~VkPipelineObject();

    void prepare();

    RBPipelineHandle get_pipeline_handle() const
    {
        assert(vk_pipeline != VK_NULL_HANDLE); 
        return vk_pipeline;
    }
    
    
    VkPipelineLayout get_pipeline_layout() const
    {
        assert(vk_pipeline != VK_NULL_HANDLE); 
        return pipeline_layout;
    }
    VkPipeline get_or_create_pipeline(VkRenderPass render_pass);
    
    RenderResourceInstance* query_unique_resource_instance(RenderResource* resource, uint32_t instance_id = 0) override;
    RenderResourceInstance* create_resource_instance(RenderResource* resource) override;
    
    void update_buffers();

    
    std::optional<GraphicsPipelineDesc> pipeline_desc;
    
    void reflect_shader(const VkShader& shader, ShaderStage stage);

    const std::map<ShaderStage, PipelineReflection>& get_reflection() const
    {
        return pipeline_reflection;
    }

public:
    vk::Instance& instance;
    vk::SwapchainControl& swapchain;
    vk::BufferManager& buffer_manager;

    VkPipeline vk_pipeline = VK_NULL_HANDLE;
    
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    
    std::vector<VkShader> shaders;
    
    std::map<ShaderStage, PipelineReflection> pipeline_reflection;
    
    
    
    std::map<UniqueResourcePair, VkRenderResourceInstance*> unique_resource_instances; 
    std::vector<std::unique_ptr<VkRenderResourceInstance>> instances;
    
    std::map<VkRenderResource*, VkRenderResourcePipelineInfo> resources_pipeline_info;
    
    struct ResorceInstancePipelineData
    {
        std::vector<RBDescriptorSet> sets_per_frame = {};
        std::vector<std::vector<RBBufferHandle>> buffers = {}; // [binding][frame]
    };
    
    std::map<VkRenderResourceInstance*, ResorceInstancePipelineData> instance_pipeline_data;
    
};
