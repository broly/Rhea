#ifndef RESOURCES_PARTICLES
#define RESOURCES_PARTICLES

// Sprites of the particle systems (ParticleRenderer, src/game/particle_renderer.*): written by the CPU every
// frame, far to near. Six vertices per particle, no vertex buffer: gl_VertexIndex / 6 is the particle.

#ifndef SET_PARTICLES
    #define SET_PARTICLES 0
    #error "SET_PARTICLES definition is missing. Provide this resource: particles"
#endif
#ifndef BINDING_SSBO_PARTICLES
    #define BINDING_SSBO_PARTICLES 0
    #error "BINDING_SSBO_PARTICLES definition is missing. Provide this resource: particles"
#endif

// GPUParticle (src/particles/particles.ixx)
struct GPUParticle
{
    vec4 position_size;     // xyz: world, center of the sprite, w: width (m)
    vec4 color;             // linear rgb (x intensity), a: opacity
    vec4 velocity_stretch;  // xyz: world (m/s), w: s of flight a velocity facing sprite is longer by
    vec4 shape;             // x: rotation (radians), y: frame of the grid, z: soft distance (m), w: additive (0 .. 1)
    uvec4 info;             // x: texture (0: a soft round dot), y: columns | rows << 16, z: flags, w: aspect (float bits)
};

const uint PARTICLE_LIT = 1u;               // x light of the scene (push constants)
const uint PARTICLE_VELOCITY_FACING = 2u;   // turns around its velocity, stretched along it
const uint PARTICLE_SMOOTH = 4u;            // cross fades to the next frame
const uint PARTICLE_LOOP = 8u;              // the frame after the last one is the first
const uint PARTICLE_MIRRORED = 16u;         // the texture left to right

layout(std430, set = SET_PARTICLES, binding = BINDING_SSBO_PARTICLES)
readonly buffer ParticleBuffer
{
    GPUParticle particles[];
} u_particles;

#endif // RESOURCES_PARTICLES
