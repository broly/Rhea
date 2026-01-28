export module render:pipeline_object;

import :handle_types;
import :render_resource;

export class PipelineObject
{
public:
    virtual RenderResourceInstance* query_unique_resource_instance(RenderResource* resource) = 0;
    virtual RenderResourceInstance* create_resource_instance(RenderResource* resource) = 0;
    
    Name debug_name;
    uint64_t permutation_value;
};

