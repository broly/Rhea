export module render:pipeline_object;

import :handle_types;
import :render_resource;

export class PipelineObject
{
public:
    virtual void prepare(const GraphicsPipelineDesc& in_desc) = 0;
};

