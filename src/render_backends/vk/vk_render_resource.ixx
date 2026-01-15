export module vk:render_resource;
import render;

import :pipeline;
import :render_resource_instance;
import <memory>;


template<typename T>
using frame_list = std::vector<T>;

struct VkRenderResourcePipelineInfo
{
    DescriptorSetLayoutDesc descritor_set_layout_desc;
    RBDescriptorSetLayout layout;
    frame_list<RBDescriptorSet> sets_per_frame;
    std::vector<frame_list<RBBufferHandle>> buffers;
};

class VkRenderResource : public RenderResource
{
public:
    VkRenderResource(const RenderResourceDesc& desc, vk::BufferManager& in_buffer_manager, class VkRenderBackend& in_backend);

    vk::BufferManager& buffer_manager;
    VkRenderBackend& backend;
    
    RenderResourceInstance* create_instance() override;
    RenderResourceInstance* query_single() override;

    virtual void provide(class PipelineObject* pipeline_object) override;
    
    virtual RBDescriptorSetLayout get_descriptor_set_layout(class PipelineObject* pipeline_object) override;
    
    std::map<VkPipelineObject*, VkRenderResourcePipelineInfo> info_by_pipeline;
    
    RenderResourceInstance* single_resource = nullptr;
    
    std::vector<std::unique_ptr<VkRenderResourceInstance>> instances;
};
