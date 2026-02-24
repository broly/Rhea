module vk:render_resource;

import <cassert>;
import <set>;
import :render_backend;
#include "common/assertion_macros.h"
#include "profiling/profile.h"

            

VkRenderResource::VkRenderResource(const RenderResourceDesc& desc, vk::BufferManager& in_buffer_manager,
    class VkRenderBackend& in_backend)
    : RenderResource(desc)
    , buffer_manager(in_buffer_manager)
    , backend(in_backend)
{}

std::shared_ptr<RenderResourceInstance> VkRenderResource::query_unique(
    uint32_t unique_id, uint32_t instance_id)
{
    return std::static_pointer_cast<RenderResourceInstance>(
        backend.pipeline_manager.query_single_resource_instance(this, unique_id, instance_id, desc.usage)
    );
}

std::shared_ptr<RenderResourceInstance> VkRenderResource::query_single(uint32_t instance_id)
{
    return std::static_pointer_cast<RenderResourceInstance>(
        backend.pipeline_manager.query_single_resource_instance(this, 0, instance_id, desc.usage)
    );
}


