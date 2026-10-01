module game;

import :occlusion_culling;

import std.compat;
import assertions;

import render;
import rhmath;
import profile;

import log;
#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"

DEFINE_LOGGER(LogOcclusionCulling, Display);

namespace
{
    // size of the buffer parameter of a resource
    size_t buffer_size(const RenderResource& resource, Name variable)
    {
        return resource.find_var_checked(variable).parameter.initial_buffer_size.value_or(0);
    }

    uint32_t previous_power_of_two(uint32_t value)
    {
        return value <= 1 ? 1u : 1u << (31 - std::countl_zero(value));
    }

    // local sizes of the shaders
    constexpr uint32_t cull_group = 64;
    constexpr uint32_t hzb_group = 8;
    // bytes of DrawList::Command (VkDrawIndexedIndirectCommand)
    constexpr uint32_t command_size = 20;
}

void OcclusionCulling::init(Renderer& renderer, RenderBackend& in_backend)
{
    backend = &in_backend;

    occlusion_resource = renderer.find_resource("occlusion");
    depth_resource = renderer.find_resource("occlusion_depth");
    checkf(occlusion_resource && depth_resource, "occlusion culling resources are missing (assets/render/resources)");

    auto model = renderer.find_model("occlusion");
    checkf(model, "material model 'occlusion' is missing (assets/render/schemas/occlusion.json)");
    hzb_family = renderer.query_pipeline_family("HZBBuild", model);
    cull_family = renderer.query_pipeline_family("OcclusionCull", model);

    late_offset = uint32_t(buffer_size(*occlusion_resource, "u_cull_commands") / command_size / 2);
    visibility_capacity = uint32_t(buffer_size(*occlusion_resource, "u_visibility") / sizeof(uint32_t));
    hzb_capacity = uint32_t(buffer_size(*occlusion_resource, "u_hzb") / sizeof(float));
}

void OcclusionCulling::prepare(RenderGraphContext& ctx, RBImageHandle depth, Extent extent)
{
    // pipelines are requested every frame: hot reload drops the PSO cache
    hzb_pipeline = hzb_family->request_pipeline({});
    cull_pipeline = cull_family->request_pipeline({});

    depth_resource->update_image("u_depth", depth, { .frame = ctx.frame });
    depth_extent = extent;

    // level 0: a power of two of about half the screen, every texel covers 2-4 pixels per axis
    hzb_width = previous_power_of_two(std::max(extent.width / 2, 1u));
    hzb_height = previous_power_of_two(std::max(extent.height / 2, 1u));
    hzb_levels = 0;
    uint32_t floats = 0;
    for (uint32_t width = hzb_width, height = hzb_height; ; width = std::max(width / 2, 1u), height = std::max(height / 2, 1u))
    {
        floats += width * height;
        ++hzb_levels;
        if (width == 1 && height == 1)
            break;
    }
    checkf(floats <= hzb_capacity, "Hierarchical depth of %ux%u needs %u floats, the buffer holds %u (assets/render/resources/occlusion.json)",
        hzb_width, hzb_height, floats, hzb_capacity);
}

void OcclusionCulling::cull(RenderGraphContext& ctx, RenderResource* camera, RenderResource* draw_list, bool late,
    uint32_t first_command, uint32_t count)
{
    PROFILE("OcclusionCulling::cull");
    checkf(first_command + count <= late_offset, "Occlusion culling: %u base pass draws, the command copies hold %u",
        first_command + count, late_offset);

    // the copies are rewritten: the draws of the last frame (and of the early phase) read them
    backend->cmd_buffer_barrier(ctx.cmd, RenderBackend::BufferBarrier::draw_to_compute);
    if (count > 0)
    {
        ctx.bind_pipeline(cull_pipeline);
        ctx.bind(camera, draw_list, occlusion_resource);
        ctx.push_constants(OcclusionCullPushConstants{
            .first_command = first_command,
            .command_count = count,
            .phase = late ? 1u : 0u,
            .late_offset = late_offset,
            .hzb_width = hzb_width,
            .hzb_height = hzb_height,
            .hzb_levels = hzb_levels,
            .visibility_capacity = visibility_capacity,
        });
        ctx.compute({ (count + cull_group - 1) / cull_group, 1, 1 });
    }
    backend->cmd_buffer_barrier(ctx.cmd, RenderBackend::BufferBarrier::compute_to_draw);
}

void OcclusionCulling::build_hzb(RenderGraphContext& ctx)
{
    PROFILE("OcclusionCulling::build_hzb");

    // the late test of the last frame read the levels
    backend->cmd_buffer_barrier(ctx.cmd, RenderBackend::BufferBarrier::draw_to_compute);
    ctx.bind_pipeline(hzb_pipeline);
    ctx.bind(occlusion_resource, depth_resource);

    uint32_t source_offset = 0;
    uint32_t offset = 0;
    // level 0 reads the depth buffer
    uint32_t source_width = depth_extent.width;
    uint32_t source_height = depth_extent.height;
    uint32_t width = hzb_width;
    uint32_t height = hzb_height;
    for (uint32_t level = 0; level < hzb_levels; ++level)
    {
        if (level > 0)
            backend->cmd_buffer_barrier(ctx.cmd, RenderBackend::BufferBarrier::compute_to_compute);
        ctx.push_constants(HZBPushConstants{
            .level = level,
            .src_offset = source_offset,
            .dst_offset = offset,
            .src_width = source_width,
            .src_height = source_height,
            .dst_width = width,
            .dst_height = height,
        });
        ctx.compute({ (width + hzb_group - 1) / hzb_group, (height + hzb_group - 1) / hzb_group, 1 });

        source_offset = offset;
        source_width = width;
        source_height = height;
        offset += width * height;
        width = std::max(width / 2, 1u);
        height = std::max(height / 2, 1u);
    }
    backend->cmd_buffer_barrier(ctx.cmd, RenderBackend::BufferBarrier::compute_to_compute);
}

RBBufferHandle OcclusionCulling::get_commands_buffer() const
{
    return occlusion_resource->query_single(0)->get_ssbo_handle("u_cull_commands", std::nullopt);
}
