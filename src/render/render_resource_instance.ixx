export module render:render_resource_instance;

import <string>;
import :pipeline_desc;
import name;

export struct UpdateImageParams
{
    std::optional<RBFrameHandle> frame = std::nullopt;
    uint32_t layer_index = 0;
    bool cubemap = false;
    uint32_t array_index = 0;
    uint32_t array_layers = 1;
    bool as_array_2d = false;
};

export class RenderResourceInstance
{
public:
    virtual ~RenderResourceInstance() {}
    
    struct CachedTypedUBOCopy
    {
        
    };
    
    template<typename T>
    struct TCachedTypedUBOCopy : public CachedTypedUBOCopy
    {
        TCachedTypedUBOCopy(const T& in_data) 
            : data(in_data)
        {}
        T data;
    };
    
    std::map<Name, std::unique_ptr<CachedTypedUBOCopy>> CachedUBO;
    
    template<typename T>
    void update_uniform_buffer(Name buffer_name, const T& data, RBFrameHandle frame)
    {
        CachedUBO[buffer_name] = std::make_unique<TCachedTypedUBOCopy<T>>(data);
        update_uniform_buffer_impl(buffer_name, sizeof(T), (void*)&data, frame);
    }
    
    virtual void update_image(Name buffer_name, RBImageHandle image_handle, const UpdateImageParams& update_params) = 0;
    
    virtual void bind(RBCommandList command_list, RBFrameHandle frame) = 0;
    
    virtual void update_uniform_buffer_impl(Name buffer_name, size_t size, void* data, RBFrameHandle frame) = 0;
    
    virtual void update_tlas(Name name, RBAccelStruct tlas, RBFrameHandle frame) = 0;
    
    virtual void update_ssbo(Name buffer_name, size_t size, void* data, std::optional<RBFrameHandle> frame = std::nullopt) = 0;
    
    virtual void update_ssbo_element(Name buffer_name, size_t element_size, size_t index, const void* data, std::optional<RBFrameHandle> frame) = 0;
    
};