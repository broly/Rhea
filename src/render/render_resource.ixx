export module render:render_resource;

import <string>;
import :graphics_pipeline_desc;
import :render_resource_instance;
import :pipeline_object;
import name;

struct RenderResourceVariableDesc
{
    MatModel_Parameter parameter;
    RBSampler sampler;
    size_t size;
};

export struct RenderResourceDesc
{
    Name name;
    
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

    virtual std::shared_ptr<RenderResourceInstance> query_single(RBPipelineLayout pipeline_layout, uint32_t instance_id = 0) = 0;
    virtual std::shared_ptr<RenderResourceInstance> query_single(PipelineObject*, uint32_t instance_id = 0);
    virtual std::shared_ptr<RenderResourceInstance> query_unique(RBPipelineLayout pipeline_layout, uint32_t unique_id, uint32_t instance_id) = 0;

    const RenderResourceDesc desc;
};

