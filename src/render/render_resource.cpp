module render:render_resource;

import :pipeline_object;

RenderResourceInstance* RenderResource::query_single(PipelineObject* pipeline_object, uint32_t instance_id)
{
    return pipeline_object->query_unique_resource_instance(this, instance_id);
}