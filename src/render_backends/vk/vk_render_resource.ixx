export module vk:render_resource;
import render;

import :render_resource_instance;
import <memory>;


template<typename T>
using frame_list = std::vector<T>;


class VkRenderResource : public RenderResource
{
public:
    VkRenderResource(const RenderResourceDesc& desc, vk::BufferManager& in_buffer_manager, class VkRenderBackend& in_backend);

    virtual std::shared_ptr<RenderResourceInstance> query_single(RBPipelineLayout pipeline_layout, uint32_t instance_id) override;
    virtual std::shared_ptr<RenderResourceInstance> query_unique(RBPipelineLayout pipeline_layout, uint32_t unique_id, uint32_t instance_id) override;
    
    vk::BufferManager& buffer_manager;
    VkRenderBackend& backend;
    
};
