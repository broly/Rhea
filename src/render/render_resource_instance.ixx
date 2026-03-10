export module render:render_resource_instance;

import <string>;
import :pipeline_desc;
import name;

export class RenderResourceInstance
{
public:
    
    template<typename T>
    void update_uniform_buffer(Name buffer_name, const T& data, RBFrameHandle frame)
    {
        update_uniform_buffer_impl(buffer_name, sizeof(T), (void*)&data, frame);
    }
    
    virtual void update_image(Name buffer_name, RBImageHandle image_handle, RBFrameHandle frame, uint32_t array_index = 0, bool cubemap = false) = 0;
    
    virtual void bind(RBCommandList command_list, RBFrameHandle frame) = 0;
    
    virtual void update_uniform_buffer_impl(Name buffer_name, size_t size, void* data, RBFrameHandle frame) = 0;
    
    virtual void update_tlas(Name name, RBAccelStruct tlas, RBFrameHandle frame) = 0;
    
};