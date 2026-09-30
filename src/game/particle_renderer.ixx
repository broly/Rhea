module;

#include <json/value.h>

export module game:particle_renderer;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import render;
import render_scene;
import assets;
import glm;
import name;
import cvar;
import rhmath;
#include "object/object_reflection_macro.h"

// particles.vert
struct ParticlePushConstants
{
    glm::vec4 ambient;      // rgb: what the sky sends to a lit particle
};
RH_REGISTER_TYPE(ParticlePushConstants)

export cvar::Var<bool> cv_particles_enabled(
    "render.particles.enabled", true, "Particle systems are drawn (their simulation goes on)");

// Particles of the main view: the sprites the particle systems of the level produced this frame
// (SceneViewProcessor_Particles, module particles) drawn over the lit frame.
//
//   prepare  sorts them far to near and writes them into the frame's copy of the resource "particles"; the
//            textures of new effects enter the texture array
//   draw     pass "Particles": one draw of six vertices per sprite (particles.vert builds the quads),
//            depth tested against the scene, blended with premultiplied alpha (covering and glowing sprites
//            in one order)
//
// Sprites beyond what the buffer holds (initial_buffer_size in assets/render/resources/particles.json) are
// dropped, the nearest ones kept.
export class ParticleRenderer
{
public:
    struct FrameInputs
    {
        glm::vec3 camera_position;
        // radiance a lit particle takes from the sky (the lights come from the light UBO)
        glm::vec3 ambient;
    };

    void init(Renderer& renderer, RenderBackend& backend);

    void prepare(RenderGraphContext& ctx, SceneView& scene_view, const FrameInputs& inputs);

    // Anything to draw this frame (set by prepare)
    bool has_particles() const { return count > 0; }
    uint32_t get_count() const { return count; }

    // Blends the sprites over the bound target (the lit frame, with the depth of the scene)
    void draw(RenderGraphContext& ctx, RenderResource* camera, RenderResource* light, RenderResource* gbuffer);

private:
    Renderer* renderer = nullptr;
    RenderBackend* backend = nullptr;

    RenderResource* particles_resource = nullptr;
    RenderResource* textures_resource = nullptr;

    std::shared_ptr<PipelineFamily> family;
    PipelineObject* pipeline = nullptr;

    // sprite textures already in the texture array
    std::set<uint32_t> registered_textures;
    // by distance from the camera, far to near
    std::vector<std::pair<float, uint32_t>> order;
    uint32_t count = 0;
    glm::vec3 ambient = glm::vec3(0.0f);
    bool overflow_reported = false;
};
