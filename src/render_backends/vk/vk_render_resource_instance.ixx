export module vk:render_resource_instance;

import render;
import :buffer_mgr;


class VkRenderResourceInstance : public RenderResourceInstance
{
public:
    VkRenderResourceInstance(vk::BufferManager& buffer_manager, const class VkRenderResource& in_resource, ResourceUsageType in_usage);

    virtual void update_uniform_buffer_impl(PipelineObject* pipeline_object, 
        const char* buffer_name_SUBOPTIMAL, size_t size, void* data);
    
    void update_image(class PipelineObject* pipeline_object, const char* buffer_name_SUBOPTIMAL, RBImageHandle image_handle) override;
    
    void bind(class PipelineObject* pipeline_object, RBCommandList command_list) override;

    vk::BufferManager& buffer_manager;
    const VkRenderResource& resource;
    ResourceUsageType usage;
};
