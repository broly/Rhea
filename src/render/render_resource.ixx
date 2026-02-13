export module render:render_resource;

import <string>;
import :graphics_pipeline_desc;
import :render_resource_instance;
import name;

struct RenderResourceVariableDesc
{
    Name name;
    
    Name set;
    Name binding;
    RBSampler sampler;
    
    size_t size;
};

export struct RenderResourceDesc
{
    Name name;
    
    ShaderStage stages = ShaderStage::all;
    ResourceUsageType usage_type = ResourceUsageType::persistent;
    
    std::vector<RenderResourceVariableDesc> variables;
    Name set;
};

export class RenderResource
{
public:
    virtual ~RenderResource() = default;

    RenderResource(const RenderResourceDesc& in_desc)
        : desc(in_desc)
    {}

    virtual RenderResourceInstance* query_single(PipelineObject* pipeline_object, uint32_t instance_id = 0);

    const RenderResourceDesc desc;
};

