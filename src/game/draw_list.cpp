module game;

import :draw_list;

import std.compat;
import assertions;
import render;

#include "common/assertion_macros.h"

void DrawList::init(Renderer& renderer, RenderBackend& in_backend)
{
    backend = &in_backend;
    resource = renderer.find_resource("draw_list");
    checkf(resource, "resource 'draw_list' is missing (assets/render/resources/draw_list.json)");
}

void DrawList::begin_frame(RBFrameHandle in_frame)
{
    frame = in_frame;
    auto instance = resource->query_single(0);

    const std::span<std::byte> record_memory = instance->map_ssbo("u_draw_records", frame);
    records = { reinterpret_cast<GPUDrawRecord*>(record_memory.data()), record_memory.size() / sizeof(GPUDrawRecord) };

    const std::span<std::byte> command_memory = instance->map_ssbo("u_draw_commands", frame);
    commands = { reinterpret_cast<Command*>(command_memory.data()), command_memory.size() / sizeof(Command) };
    commands_buffer = instance->get_ssbo_handle("u_draw_commands", frame);

    const std::span<std::byte> bounds_memory = instance->map_ssbo("u_draw_bounds", frame);
    bounds = { reinterpret_cast<Bounds*>(bounds_memory.data()), bounds_memory.size() / sizeof(Bounds) };

    num_records = 0;
    num_commands = 0;
}

uint32_t DrawList::add_record(const GPUDrawRecord& record)
{
    checkf(num_records < records.size(),
        "Draw list full: %zu records per frame (initial_buffer_size in assets/render/resources/draw_list.json)", records.size());
    records[num_records] = record;
    return num_records++;
}

void DrawList::add_draw(const GPUDrawRecord& record, const MeshIndexRange& indices, const AABB* world_bounds)
{
    const uint32_t record_index = add_record(record);
    if (world_bounds)
    {
        checkf(record_index < bounds.size(), "Draw list full: %zu bounds per frame (assets/render/resources/draw_list.json)",
            bounds.size());
        bounds[record_index] = { glm::vec4(world_bounds->min, 0.0f), glm::vec4(world_bounds->max, 0.0f) };
    }
    checkf(num_commands < commands.size(),
        "Draw list full: %zu indirect commands per frame (initial_buffer_size in assets/render/resources/draw_list.json)", commands.size());
    commands[num_commands++] = Command{
        .index_count = indices.index_count,
        .instance_count = 1,
        .first_index = indices.first_index,
        .vertex_offset = 0,
        .first_instance = record_index,
    };
}

void DrawList::add_vertex_draw(const GPUDrawRecord& record, uint32_t vertex_count)
{
    const uint32_t record_index = add_record(record);
    checkf(num_commands < commands.size(),
        "Draw list full: %zu indirect commands per frame (initial_buffer_size in assets/render/resources/draw_list.json)", commands.size());
    // VkDrawIndirectCommand: vertex_count, instance_count, first_vertex, first_instance
    commands[num_commands++] = Command{
        .index_count = vertex_count,
        .instance_count = 1,
        .first_index = 0,
        .vertex_offset = int32_t(record_index),
        .first_instance = 0,
    };
}

void DrawList::bind(RenderGraphContext& ctx) const
{
    resource->bind(ctx.cmd, ctx.frame, 0);
}

void DrawList::draw_indirect(RenderGraphContext& ctx, uint32_t index_block, uint32_t first_command, uint32_t count,
    std::optional<RBBufferHandle> gpu_commands, uint32_t command_offset) const
{
    if (count == 0)
        return;
    backend->bind_mesh_index_block(ctx.cmd, index_block);
    backend->draw_indexed_indirect(ctx.cmd, gpu_commands.value_or(commands_buffer), frame,
        uint64_t(command_offset + first_command) * sizeof(Command), count, sizeof(Command));
}

void DrawList::draw_vertex_indirect(RenderGraphContext& ctx, uint32_t first_command, uint32_t count) const
{
    backend->draw_indirect(ctx.cmd, commands_buffer, frame, uint64_t(first_command) * sizeof(Command), count,
        sizeof(Command));
}
