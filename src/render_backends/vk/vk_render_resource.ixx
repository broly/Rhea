export module vk:render_resource;
import render;

import :pipeline;
import :render_resource_instance;
import <memory>;


struct VkRenderResourcePipelineInfo
{
    DescriptorSetLayoutDesc descritor_set_layout_desc;
    RBDescriptorSetLayout layout;
    std::optional<RBDescriptorSet> set;
    std::vector<std::optional<RBBufferHandle>> buffers;
};

class VkRenderResource : public RenderResource
{
public:
    VkRenderResource(const RenderResourceDesc& desc, vk::BufferManager& in_buffer_manager, class VkRenderBackend& in_backend);

    vk::BufferManager& buffer_manager;
    VkRenderBackend& backend;
    
    RenderResourceInstance* create_instance() override;
    RenderResourceInstance* query_single() override;
    
    void bind(PipelineObject* pipeline_object) override;
    
    virtual void provide(class PipelineObject* pipeline_object) override;
    
    std::map<VkPipelineObject*, VkRenderResourcePipelineInfo> info_by_pipeline;
    
    RenderResourceInstance* single_resource = nullptr;
    
    std::vector<std::unique_ptr<VkRenderResourceInstance>> instances;
};
