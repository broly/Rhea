export module vk:render_resource_instance;

import render;
import :buffer_mgr;
import name;



class VkRenderResourceInstance : public RenderResourceInstance
{
public:
    
    VkRenderResourceInstance(vk::BufferManager& buffer_manager, const class VkRenderResource* in_resource, ResourceUsage in_usage, RBPipelineLayout in_pipeline_layout);

    virtual void update_uniform_buffer_impl(Name buffer_name, size_t size, void* data, RBFrameHandle frame);
    
    void update_image(Name buffer_name, RBImageHandle image_handle, RBFrameHandle
                      frame, uint32_t array_index = 0, bool cubemap = false) override;
    
    void bind(RBCommandList command_list, RBFrameHandle frame) override;

    vk::BufferManager& buffer_manager;
    const VkRenderResource* resource;
    ResourceUsage usage;
    VkPipelineLayout pipeline_layout;
    
    bool has_set_per_frame = false;
    RBDescriptorSet set_per_frame[5];
    std::optional<uint32_t> set_index;

    // std::map<VkPipelineObject*, PerPipelineData> per_pipeline;
};
