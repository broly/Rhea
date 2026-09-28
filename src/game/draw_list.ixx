export module game:draw_list;

import std.compat;
import render;

// Mesh draws of the frame, in the frame's copy of the resource "draw_list" (host visible, rewritten every
// frame, shaders/resources/draw_list.glsl):
//   - records: what one draw draws (mesh, primitive, material, pass specific value). A draw's first_instance
//     is its record index: shaders read it through gl_InstanceIndex.
//   - commands: VkDrawIndirectCommand layout, consumed by RenderBackend::draw_indirect.
// Passes group their draws by pipeline: one indirect draw per pipeline instead of one draw per primitive
// (only the draws that must keep their order across pipelines, like translucency, draw one by one).
//
//     const uint32_t first = draw_list.command_count();
//     for (...) draw_list.add_draw(record, vertex_count);
//     ctx.bind_pipeline(pipeline); ... draw_list.bind(ctx);
//     draw_list.draw_indirect(ctx, first, draw_list.command_count() - first);
export class DrawList
{
public:
    // layout of VkDrawIndirectCommand
    struct Command
    {
        uint32_t vertex_count;
        uint32_t instance_count;
        uint32_t first_vertex;
        uint32_t first_instance;
    };

    void init(Renderer& renderer, RenderBackend& backend);

    // before the passes of a frame: the frame's buffers, counters reset (the frame's fence was waited)
    void begin_frame(RBFrameHandle frame);

    // record for a direct draw: draw it with ctx.draw(vertex_count, 0, <returned index>)
    uint32_t add_record(const GPUDrawRecord& record);

    // record + indirect command drawing it (a single instance of vertex_count vertices)
    void add_draw(const GPUDrawRecord& record, uint32_t vertex_count);

    uint32_t command_count() const { return num_commands; }

    // the records of the frame, after binding the pipeline of the draws
    void bind(RenderGraphContext& ctx) const;

    // commands [first_command, first_command + count) with the bound pipeline
    void draw_indirect(RenderGraphContext& ctx, uint32_t first_command, uint32_t count) const;

private:
    RenderResource* resource = nullptr;
    RenderBackend* backend = nullptr;
    RBFrameHandle frame{};
    std::span<GPUDrawRecord> records;
    std::span<Command> commands;
    RBBufferHandle commands_buffer{};
    uint32_t num_records = 0;
    uint32_t num_commands = 0;
};
