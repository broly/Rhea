export module render:render_resource;

import <string>;
import :pipeline_desc;
import :render_resource_instance;
import :pipeline_object;
import name;
#include "common/assertion_macros.h"
#include "common/type_macros.h"

export struct RenderResourceVariableDesc
{
    MatModel_Parameter parameter;
    RBSampler sampler;
    size_t size;
    uint32_t binding;
};

export struct RenderResourceDesc
{
    Name name;
    
    ResourceUsage usage = ResourceUsageType::persistent;
    
    std::vector<RenderResourceVariableDesc> variables;
    Name set;
    Mask<ShaderStage> allowed_stages;
    
    const RenderResourceVariableDesc* find_variable(Name var_name) const
    {
        for (auto& var : variables)
        {
            if (var.parameter.variable == var_name)
                return &var;
        }
        return nullptr;
    }
    
    const RenderResourceVariableDesc& find_variable_checked(Name var_name) const
    {
        auto res = find_variable(var_name);
        checkf(res, "Could not find %s", var_name.to_string().c_str());
        return *res;
    }
    
    uint32_t set_index;  // assigned at runtime
};

export class RenderResource
{
public:
    NON_COPYABLE(RenderResource);
    
    virtual ~RenderResource() = default;

    RenderResource(const RenderResourceDesc& in_desc)
        : desc(in_desc)
    {}
    
    
    template<typename T>
    void update_uniform_buffer(Name buffer_name, const T& data, RBFrameHandle frame, uint32_t instance_id = 0)
    {
        update_uniform_buffer_impl(buffer_name, sizeof(T), (void*)&data, frame, instance_id);
    }
    
    void update_image(Name buffer_name, RBImageHandle image_handle, const UpdateImageParams& params = {}, uint32_t instance_id = 0)
    {
        query_single(instance_id)->update_image(buffer_name, image_handle, params);
    }
    
    void bind(RBCommandList command_list, RBFrameHandle frame, uint32_t instance_id = 0)
    {
        query_single(instance_id)->bind(command_list, frame);
    }
    
    void update_uniform_buffer_impl(Name buffer_name, size_t size, void* data, RBFrameHandle frame, uint32_t instance_id = 0)
    {
        query_single(instance_id)->update_uniform_buffer_impl(buffer_name, size, data, frame);
    }
    
    void update_tlas(Name name, RBAccelStruct tlas, RBFrameHandle frame, uint32_t instance_id = 0)
    {
        query_single(instance_id)->update_tlas(name, tlas, frame);
    }
    
    void update_ssbo(Name buffer_name, size_t size, void* data, std::optional<RBFrameHandle> frame = std::nullopt, uint32_t instance_id = 0)
    {
        query_single(instance_id)->update_ssbo(buffer_name, size, data, frame);
    }
    
    void update_ssbo_element(Name buffer_name, size_t element_size, uint32_t index, const void* data, std::optional<RBFrameHandle> frame = std::nullopt, uint32_t instance_id = 0)
    {
        query_single(instance_id)->update_ssbo_element(buffer_name, element_size, index, data, frame);
    }

    virtual std::shared_ptr<RenderResourceInstance> query_single(uint32_t instance_id = 0) = 0;
    virtual std::shared_ptr<RenderResourceInstance> query_unique(uint32_t unique_id, uint32_t instance_id) = 0;

    const RenderResourceDesc desc;
};

