module render:render_resource;

import :pipeline_object;

RenderResourceInstance* RenderResource::query_single(PipelineObject* pipeline_object)
{
    return pipeline_object->query_unique_resource_instance(this);
}