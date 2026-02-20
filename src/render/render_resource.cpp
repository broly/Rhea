module render:render_resource;

import :pipeline_object;
import :pipeline_family;

std::shared_ptr<RenderResourceInstance> RenderResource::query_single(PipelineObject* object,
    uint32_t instance_id)
{
    return query_single(object->get_layout(), instance_id);
}
