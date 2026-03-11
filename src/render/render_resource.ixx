export module render:render_resource;

import <string>;
import :pipeline_desc;
import :render_resource_instance;
import :pipeline_object;
import name;
#include "common/assertion_macros.h"

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
    virtual ~RenderResource() = default;

    RenderResource(const RenderResourceDesc& in_desc)
        : desc(in_desc)
    {}

    virtual std::shared_ptr<RenderResourceInstance> query_single(uint32_t instance_id = 0) = 0;
    virtual std::shared_ptr<RenderResourceInstance> query_unique(uint32_t unique_id, uint32_t instance_id) = 0;

    const RenderResourceDesc desc;
};

