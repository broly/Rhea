module;

#include <json/value.h>

export module game:occlusion_culling;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import render;
import rhmath;
import cvar;
#include "object/object_reflection_macro.h"

// hzb_build.comp
struct HZBPushConstants
{
    uint32_t level;
    uint32_t src_offset;
    uint32_t dst_offset;
    uint32_t src_width;
    uint32_t src_height;
    uint32_t dst_width;
    uint32_t dst_height;
};
RH_REGISTER_TYPE(HZBPushConstants)

// occlusion_cull.comp
struct OcclusionCullPushConstants
{
    uint32_t first_command;
    uint32_t command_count;
    uint32_t phase;
    uint32_t late_offset;
    uint32_t hzb_width;
    uint32_t hzb_height;
    uint32_t hzb_levels;
    uint32_t visibility_capacity;
};
RH_REGISTER_TYPE(OcclusionCullPushConstants)

export cvar::Var<bool> cv_occlusion_culling(
    "render.occlusion_culling", true, "Base pass draws hidden behind others are culled on the GPU (hierarchical depth)");

// Two phase GPU occlusion culling of the base pass (GenericRenderGraph, main graph only). The CPU puts the draws
// in the frustum into the draw list as before, then:
//   pass OcclusionCullEarly   a copy of the commands draws what was visible at the end of the last frame
//   pass GeometryBase         the early draws
//   pass HZB                  the hierarchical depth of the early draws (farthest depth per texel, mip chain)
//   pass OcclusionCullLate    tests the bounds of every draw against it: draws what became visible, remembers
//                             what is visible now (by render primitive id, for the next frame)
//   pass GeometryBaseLate     the late draws, over the early ones
// Nothing is drawn twice, nothing visible is missing for a frame (a disoccluded object is found by the late
// test of the same frame). Resources "occlusion" (GPU only buffers) and "occlusion_depth".
export class OcclusionCulling
{
public:
    void init(Renderer& renderer, RenderBackend& backend);

    // before the passes: the depth buffer of the frame and its size (hierarchical depth levels)
    void prepare(RenderGraphContext& ctx, RBImageHandle depth, Extent extent);

    // one invocation per command [first_command, first_command + count) of the draw list (base pass draws)
    void cull(RenderGraphContext& ctx, RenderResource* camera, RenderResource* draw_list, bool late,
        uint32_t first_command, uint32_t count);
    void build_hzb(RenderGraphContext& ctx);

    // where the culled copies of the commands are: the early ones at the command indices of the draw list,
    // the late ones late_offset commands after them
    RBBufferHandle get_commands_buffer() const;
    uint32_t get_late_offset() const { return late_offset; }

    // commands the copies hold (per phase)
    uint32_t get_capacity() const { return late_offset; }

    // the hierarchical depth (level 0 size, levels) as prepare laid it out
    uint32_t get_hzb_width() const { return hzb_width; }
    uint32_t get_hzb_height() const { return hzb_height; }
    uint32_t get_hzb_levels() const { return hzb_levels; }
    RenderResource* get_resource() const { return occlusion_resource; }

private:
    RenderBackend* backend = nullptr;
    RenderResource* occlusion_resource = nullptr;
    RenderResource* depth_resource = nullptr;
    std::shared_ptr<PipelineFamily> hzb_family;
    std::shared_ptr<PipelineFamily> cull_family;
    PipelineObject* hzb_pipeline = nullptr;
    PipelineObject* cull_pipeline = nullptr;

    uint32_t late_offset = 0;
    uint32_t visibility_capacity = 0;
    uint32_t hzb_capacity = 0;          // floats

    // level 0 and the number of levels
    uint32_t hzb_width = 0;
    uint32_t hzb_height = 0;
    uint32_t hzb_levels = 0;
    Extent depth_extent;
};
