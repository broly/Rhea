module game;

import :particle_renderer;

import std.compat;
import assertions;

import render;
import render_scene;
import particles;
import assets;
import glm;
import name;
import profile;

import log;
#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"

DEFINE_LOGGER(LogParticleRenderer, Display);

void ParticleRenderer::init(Renderer& in_renderer, RenderBackend& in_backend)
{
    renderer = &in_renderer;
    backend = &in_backend;

    particles_resource = renderer->find_resource("particles");
    textures_resource = renderer->find_resource("textures");
    checkf(particles_resource && textures_resource, "particle resources are missing (assets/render/resources)");

    auto model = renderer->find_model("particles");
    checkf(model, "material model 'particles' is missing (assets/render/schemas/particles.json)");
    family = renderer->query_pipeline_family("Particles", model);
}

void ParticleRenderer::prepare(RenderGraphContext& ctx, SceneView& scene_view, const FrameInputs& inputs)
{
    PROFILE("ParticleRenderer::prepare");

    // pipelines are requested every frame: hot reload drops the PSO cache
    pipeline = family->request_pipeline({});
    ambient = inputs.ambient;
    count = 0;

    SceneViewProcessor_Particles& processor = scene_view.get_processor<SceneViewProcessor_Particles>();

    // the sprite sheets of effects seen for the first time (texture ids are the slots of the array)
    for (const TextureHandle texture : processor.textures)
    {
        if (!registered_textures.insert(texture.id).second)
            continue;
        textures_resource->update_image("u_textures_array", renderer->get_texture(texture), { .array_index = texture.id });
    }

    const std::vector<GPUParticle>& particles = processor.particles;
    if (particles.empty() || !cv_particles_enabled.get())
        return;

    // far to near: what covers is blended over what is behind it
    order.clear();
    order.reserve(particles.size());
    for (uint32_t index = 0; index < particles.size(); ++index)
    {
        const glm::vec3 offset = particles[index].position - inputs.camera_position;
        order.emplace_back(glm::dot(offset, offset), index);
    }
    std::ranges::sort(order, std::greater{}, &std::pair<float, uint32_t>::first);

    const std::span<std::byte> memory = particles_resource->query_single(0)->map_ssbo("u_particles", ctx.frame);
    const std::span<GPUParticle> records{ reinterpret_cast<GPUParticle*>(memory.data()), memory.size() / sizeof(GPUParticle) };

    // more than the buffer holds: the farthest are dropped
    const size_t skipped = order.size() > records.size() ? order.size() - records.size() : 0;
    if (skipped > 0 && !overflow_reported)
    {
        overflow_reported = true;
        LogParticleRenderer.Log<Warning>("%zu particles, the buffer holds %zu: the farthest are not drawn "
            "(initial_buffer_size in assets/render/resources/particles.json)", order.size(), records.size());
    }
    for (size_t i = skipped; i < order.size(); ++i)
        records[count++] = particles[order[i].second];
}

void ParticleRenderer::draw(RenderGraphContext& ctx, RenderResource* camera, RenderResource* light, RenderResource* gbuffer)
{
    PROFILE("ParticleRenderer::draw");

    if (count == 0)
        return;

    if (ctx.bind_pipeline(pipeline))
        ctx.bind(camera, light, gbuffer, textures_resource, particles_resource);
    ctx.push_constants(ParticlePushConstants{ .ambient = glm::vec4(ambient, 0.0f) });
    // particles.vert: six vertices per sprite
    ctx.draw(count * 6);
}
