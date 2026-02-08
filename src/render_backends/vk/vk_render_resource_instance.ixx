export module vk:render_resource_instance;

import render;
import :buffer_mgr;
import name;



class VkRenderResourceInstance : public RenderResourceInstance
{
public:
    struct PerPipelineData
    {
        std::vector<RBDescriptorSet> sets_per_frame;
        std::vector<std::vector<RBBufferHandle>> buffers; // [binding][frame]
    };
    
    VkRenderResourceInstance(vk::BufferManager& buffer_manager, const class VkRenderResource& in_resource, ResourceUsageType in_usage);

    virtual void update_uniform_buffer_impl(PipelineObject* pipeline_object,
                                            Name buffer_name, size_t size, void* data, RBFrameHandle frame);
    
    void update_image(class PipelineObject* pipeline_object, Name buffer_name, RBImageHandle image_handle, RBFrameHandle
                      frame, uint32_t array_index = 0, bool cubemap = false) override;
    
    void bind(class PipelineObject* pipeline_object, RBCommandList command_list, RBFrameHandle frame) override;

    vk::BufferManager& buffer_manager;
    const VkRenderResource& resource;
    ResourceUsageType usage;

    // std::map<VkPipelineObject*, PerPipelineData> per_pipeline;
};
