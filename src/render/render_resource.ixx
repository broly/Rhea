export module render:render_resource;

import <string>;
import :graphics_pipeline_desc;
import :render_resource_instance;

struct RenderResourceVariable
{
    std::string name;
    size_t size;
};

export struct RenderResourceDesc
{
    std::string name;
    
    ShaderStage stages = ShaderStage::all;
    ResourceUsageType usage_type = ResourceUsageType::persistent;
    
    std::vector<RenderResourceVariable> variables;
};

export class RenderResource
{
public:
    virtual ~RenderResource() = default;

    RenderResource(const RenderResourceDesc& in_desc)
        : desc(in_desc)
    {}
    
    virtual void provide(class PipelineObject* pipeline_object) = 0;
    virtual RenderResourceInstance* create_instance() = 0;
    virtual RenderResourceInstance* query_single() = 0;
    virtual void bind(PipelineObject* pipeline_object) = 0;
    
    const RenderResourceDesc desc;
};