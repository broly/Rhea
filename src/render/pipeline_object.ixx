export module render:pipeline_object;

import :handle_types;

export class PipelineObject
{
public:
    virtual RBPipelineHandle get_pipeline_handle() const = 0;
};
