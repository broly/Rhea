export module render:render_resource_instance;

import <string>;
import :graphics_pipeline_desc;
import name;

export class RenderResourceInstance
{
public:
    
    template<typename T>
    void update_uniform_buffer(class PipelineObject* pipeline_object, Name buffer_name, const T& data, RBFrameHandle frame)
    {
        update_uniform_buffer_impl(pipeline_object, buffer_name, sizeof(T), (void*)&data, frame);
    }
    
    virtual void update_image(class PipelineObject* pipeline_object, Name buffer_name,
                              RBImageHandle image_handle, RBFrameHandle frame) = 0;
    
    virtual void bind(class PipelineObject* pipeline_object, RBCommandList command_list, RBFrameHandle frame) = 0;
    
    virtual void update_uniform_buffer_impl(class PipelineObject* pipeline_object,
                                            Name buffer_name, size_t size, void* data, RBFrameHandle frame) = 0;
    
};