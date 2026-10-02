module;

#include <json/value.h>

export module particles;

import std.compat;
import glm;
import rhmath;
import linear_color;
import reflect;
import rhobject;
import dependency_collector;
import fixed_string;
import type_id;
import assets;
import render_scene;

#include "object/object_reflection_macro.h"

// Particle systems: sprites simulated on the CPU and drawn as billboards after the lit frame (flames, sparks,
// smoke, dust).
//
// An effect is a JSON file (assets/particles/<name>.json): a list of emitters. An emitter spawns particles
// of one kind, moves them, and says how they look over their life. It is made of blocks, each one aspect of
// the particles (all optional, defaults in the structs below):
//
//     { "emitters": [ {
//         "name": "sparks", "max_particles": 128, "space": "world",
//         "spawn":     { "rate": 12, "bursts": [ { "time": 0, "count": 20 } ], "duration": 0, "loop": true, "warmup": 2 },
//         "shape":     { "type": "disc", "radius": 0.2, "offset": { "x": 0, "y": 0.1, "z": 0 } },
//         "start":     { "lifetime": { "min": 1, "max": 2 }, "speed": 1.5, "direction": { "x": 0, "y": 1, "z": 0 },
//                        "spread": 20, "size": { "min": 0.01, "max": 0.03 }, "rotation": 0, "spin": 0 },
//         "forces":    { "acceleration": { "x": 0, "y": -2, "z": 0 }, "drag": 0.5, "wind": { "x": 1, "y": 0, "z": 0 },
//                        "turbulence": 2, "turbulence_size": 0.5, "turbulence_speed": 0.3 },
//         "over_life": { "size": [ { "t": 0, "value": 1 }, { "t": 1, "value": 0 } ],
//                        "color": [ { "t": 0, "color": { "r": 1, "g": 0.6, "b": 0.2, "a": 1 } }, ... ] },
//         "render":    { "texture": "textures/particles/spark.png", "columns": 1, "rows": 1, "blend": "additive",
//                        "intensity": 20, "facing": "velocity", "stretch": 0.05, "soft": 0.1 }
//     } ] }
//
// A component plays an effect at its entity (in a prefab: a child entity where the effect starts):
//
//     "ParticleSystem": { "effect": "particles/campfire.json" }
//
// Growing it: a new aspect of the particles (collision, lights, trails, sub-emitters) is a new block - a struct
// with defaults, so the effects written before it keep loading - read where the emitter is simulated
// (particles.cpp). What the simulation hands to the renderer is the list of GPUParticle of the frame
// (SceneViewProcessor_Particles): a simulation on the GPU would fill the same buffer.
//
// Effect files are reloaded when they change on disk (and by the console command particles.reload).
export
{
    // A value picked per particle, uniform in [min, max]. JSON: a number (no variation) or { "min": a, "max": b }
    struct ParticleRange
    {
        float min = 0.0f;
        float max = 0.0f;
    };
    void serialize_json_value(ParticleRange& target, const Json::Value& value, const SerializationContext& context);

    enum class ParticleSpace : uint8_t
    {
        world,      // a particle is left where it was spawned: a moving emitter leaves a trail
        local,      // the particles move (and turn, and scale) with the entity
    };

    struct ParticleBurst
    {
        float time = 0.0f;          // s after the emitter started (each loop)
        uint32_t count = 0;
    };

    // When particles appear
    struct ParticleSpawn
    {
        float rate = 10.0f;         // particles per second
        std::vector<ParticleBurst> bursts;
        float duration = 0.0f;      // s the emitter spawns for; 0: without end
        bool loop = true;           // starts over after `duration`
        // s simulated at once when the system starts: it looks as if it had been running (smoke already up)
        float warmup = 0.0f;
    };

    enum class ParticleShapeType : uint8_t
    {
        point,
        sphere,     // inside a sphere of `radius`
        box,        // inside a box of half size `extent`
        disc,       // on a horizontal disc of `radius`
    };

    // Where particles appear, in the space of the entity
    struct ParticleShape
    {
        ParticleShapeType type = ParticleShapeType::point;
        float radius = 0.0f;
        vec3 extent{ 0.0f, 0.0f, 0.0f };
        vec3 offset{ 0.0f, 0.0f, 0.0f };
    };

    // What a particle starts with
    struct ParticleStart
    {
        ParticleRange lifetime{ 1.0f, 1.0f };       // s
        ParticleRange speed{ 0.0f, 0.0f };          // m/s along `direction`, turned by up to `spread` degrees
        vec3 direction{ 0.0f, 1.0f, 0.0f };         // in the space of the entity
        float spread = 0.0f;                        // degrees, half angle of the cone; 180: any direction
        ParticleRange size{ 0.1f, 0.1f };           // m, width of the sprite
        ParticleRange rotation{ 0.0f, 0.0f };       // degrees, of the sprite around the view direction
        ParticleRange spin{ 0.0f, 0.0f };           // degrees per second
    };

    // What moves a particle
    struct ParticleForces
    {
        vec3 acceleration{ 0.0f, 0.0f, 0.0f };      // m/s^2, world: gravity (y < 0), buoyancy of hot air (y > 0)
        // 1/s: how fast a particle takes the velocity of the air around it (`wind`); 0: it keeps its own
        float drag = 0.0f;
        vec3 wind{ 0.0f, 0.0f, 0.0f };              // m/s, world
        // m/s^2: a field of swirls pushing the particles around; of `turbulence_size` m each, drifting through
        // the particles at `turbulence_speed` m/s
        float turbulence = 0.0f;
        float turbulence_size = 1.0f;
        float turbulence_speed = 0.5f;
    };

    // Keys of curves over the life of a particle, t = 0 when it is born .. 1 when it dies; by ascending t
    struct ParticleCurveKey
    {
        float t = 0.0f;
        float value = 1.0f;
    };

    struct ParticleColorKey
    {
        float t = 0.0f;
        LinearColor color;          // linear, a = opacity
    };

    // How a particle changes while it lives: scales of what it started with (no keys: no change)
    struct ParticleOverLife
    {
        std::vector<ParticleCurveKey> size;
        std::vector<ParticleColorKey> color;
    };

    enum class ParticleBlend : uint8_t
    {
        alpha,      // covers what is behind it by its opacity (smoke, dust); drawn far to near
        additive,   // adds its light (fire, sparks, glow)
    };

    enum class ParticleFacing : uint8_t
    {
        camera,     // a billboard: faces the camera, turned by the rotation of the particle
        velocity,   // turns around its velocity to face the camera, and stretches along it (sparks, rain)
    };

    // Frames of the texture, a grid of `columns` x `rows` read left to right, top to bottom
    enum class ParticleAnimation : uint8_t
    {
        over_life,  // all the frames once while the particle lives
        loop,       // `fps` frames per second, from a random frame
        random,     // one random frame per particle
    };

    // How a particle is drawn
    struct ParticleRender
    {
        std::string texture;            // asset path; rgb (sRGB) x color, a x opacity
        uint32_t columns = 1;
        uint32_t rows = 1;
        ParticleAnimation animation = ParticleAnimation::over_life;
        float fps = 24.0f;
        bool smooth = true;             // cross fades consecutive frames
        bool mirror = false;            // every other particle shows the texture mirrored left to right
        ParticleBlend blend = ParticleBlend::alpha;
        float intensity = 1.0f;         // scale of the color: above 1 the particle glows (HDR)
        ParticleFacing facing = ParticleFacing::camera;
        float stretch = 0.0f;           // velocity facing: s of flight the sprite is longer by
        float aspect = 1.0f;            // height / width of the sprite
        float soft = 0.2f;              // m: fades out over this distance before the surface behind it; 0: hard edge
        // lit by the sun and the sky instead of showing its color as it is (smoke by day and by night)
        bool lit = false;

        [[=rh::transient]] TextureHandle texture_handle;    // resolved when the effect is loaded
    };

    struct ParticleEmitter
    {
        std::string name;
        // particles alive at most: nothing is spawned beyond it
        uint32_t max_particles = 256;
        ParticleSpace space = ParticleSpace::world;

        ParticleSpawn spawn;
        ParticleShape shape;
        ParticleStart start;
        ParticleForces forces;
        ParticleOverLife over_life;
        ParticleRender render;
    };

    // An effect asset: assets/particles/<name>.json
    struct ParticleEffect
    {
        std::vector<ParticleEmitter> emitters;
    };

    // ---- runtime ----

    struct Particle
    {
        glm::vec3 position;         // in the space of the emitter (ParticleSpace)
        glm::vec3 velocity;
        float age = 0.0f;
        float lifetime = 1.0f;
        float size = 0.1f;          // at birth
        float rotation = 0.0f;      // radians
        float spin = 0.0f;          // radians per second
        float frame = 0.0f;         // first frame of its animation
        bool mirrored = false;
    };

    struct ParticleEmitterState
    {
        std::vector<Particle> particles;
        float time = 0.0f;          // s since the emitter (its loop) started
        float spawn_debt = 0.0f;    // fraction of a particle owed by `rate`
        uint32_t next_burst = 0;
        uint32_t random = 1;        // state of the generator
    };

    // Plays an effect at the entity: particles start in its space (position, rotation, scale)
    struct ParticleSystem
    {
        [[=rh::edit, =rh::read_only]] std::string effect;       // asset path of a ParticleEffect
        // off: nothing new is spawned, the particles alive play out
        [[=rh::edit]] bool emitting = true;
        [[=rh::edit, =rh::range<0.f, 4.f>]] float time_scale = 1.0f;
        // stretches the spawn shapes of the emitters (where particles appear), not the particles: a beam from
        // here to a hit is a line shape scaled to its length along z
        [[=rh::edit, =rh::speed<0.01f>]] vec3 shape_scale{ 1.0f, 1.0f, 1.0f };

        [[=rh::edit, =rh::read_only, =rh::transient]] uint32_t alive = 0;
        [[=rh::transient]] std::shared_ptr<const ParticleEffect> loaded;
        [[=rh::transient]] std::vector<ParticleEmitterState> emitters;
    };

    // One sprite as the renderer draws it (resource "particles", shaders/resources/particles.glsl: same layout)
    struct GPUParticle
    {
        glm::vec3 position;         // world, center of the sprite
        float size;                 // m, width
        glm::vec4 color;            // linear rgb (x intensity), a opacity
        glm::vec3 velocity;         // world, m/s (velocity facing)
        float stretch;              // s of flight the sprite is longer by
        float rotation;             // radians
        float frame;                // of the grid; the fraction cross fades to the next one
        float soft;                 // m
        float additive;             // 0: alpha blended, 1: added
        uint32_t texture;           // slot of the texture array (TextureHandle::id)
        uint32_t grid;              // columns | rows << 16
        uint32_t flags;             // gpu_particle_*
        float aspect;               // height / width
    };

    inline constexpr uint32_t gpu_particle_lit = 1;
    inline constexpr uint32_t gpu_particle_velocity_facing = 2;
    inline constexpr uint32_t gpu_particle_smooth = 4;      // cross fades to the next frame
    inline constexpr uint32_t gpu_particle_loop = 8;        // the frame after the last one is the first
    inline constexpr uint32_t gpu_particle_mirrored = 16;   // the texture left to right

    // System set of the particle simulation (Late, after the transforms, before the render sync)
    struct ParticleSimulation {};

    // Render side of the particles: the sprites of the frame, in no order, written by the simulation.
    // No proxies: the simulation already holds what is drawn.
    class SceneViewProcessor_Particles : public SceneViewProcessor
    {
    public:
        void process() override {}

        std::vector<GPUParticle> particles;
        // sprite textures of the effects loaded so far (the renderer puts them into the texture array)
        std::vector<TextureHandle> textures;
    };
}
RH_OBJECT(SceneViewProcessor_Particles)
