export module vk:render_resource;
import render;

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

    // std::map<VkPipelineObject*, VkRenderResourcePipelineInfo> info_by_pipeline;
    
    // RenderResourceInstance* single_resource = nullptr;
    
    // std::vector<std::unique_ptr<VkRenderResourceInstance>> instances;
};
