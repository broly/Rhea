module vk:render_resource;

import <cassert>;
import <set>;

#include "common/assertion_macros.h"
#include "profiling/profile.h"

#define check_conditional(cond, assertion, msg, ...) \
        if (cond) \
            checkf(assertion, msg, __VA_ARGS__); \
            

VkRenderResource::VkRenderResource(const RenderResourceDesc& desc, vk::BufferManager& in_buffer_manager,
    class VkRenderBackend& in_backend)
    : RenderResource(desc)
    , buffer_manager(in_buffer_manager)
    , backend(in_backend)
{}