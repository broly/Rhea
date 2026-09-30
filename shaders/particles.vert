#version 450

// Sprites of the particle systems (ParticleRenderer, pass "Particles"): a quad per particle, built here from
// its record. It faces the camera, or turns around the velocity of the particle and stretches along it.

#include "resources/camera.glsl"
#include "resources/light.glsl"
#include "resources/particles.glsl"

layout(push_constant) uniform ParticlePushConstants
{
    vec4 ambient;       // rgb: what the sky sends to a lit particle
} pc;

layout(location = 0) out vec2 v_uv;             // over the sprite, (0, 0): its top left
layout(location = 1) out vec4 v_color;
layout(location = 2) out float v_view_depth;    // m in front of the camera
layout(location = 3) flat out uint v_particle;

const float PI = 3.14159265;

// What reaches a particle that scatters light in every direction (smoke): the sky, the directional light
// and the point lights, none of them shadowed. The share it sends on to the eye, per unit of its albedo.
vec3 scene_light(vec3 position)
{
    const float phase = 0.5 / PI;
    vec3 light = pc.ambient.rgb;
    if (light_ubo.has_dir_light == 1)
        light += light_ubo.dir_light.color.rgb * phase;
    for (int i = 0; i < light_ubo.light_count; ++i)
    {
        vec3 to_light = light_ubo.lights[i].position.xyz - position;
        // as lighting.frag attenuates them
        light += light_ubo.lights[i].color.rgb * (phase / (dot(to_light, to_light) + 1.0));
    }
    return light;
}

void main()
{
    const vec2 corners[6] = vec2[](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));

    uint index = uint(gl_VertexIndex) / 6u;
    vec2 corner = corners[gl_VertexIndex % 6];
    GPUParticle particle = u_particles.particles[index];

    vec3 center = particle.position_size.xyz;
    float half_width = 0.5 * particle.position_size.w;
    float half_height = half_width * uintBitsToFloat(particle.info.w);

    vec3 camera_right = camera_ubo.inv_view[0].xyz;
    vec3 camera_up = camera_ubo.inv_view[1].xyz;

    vec3 right;
    vec3 up;
    if ((particle.info.z & PARTICLE_VELOCITY_FACING) != 0u)
    {
        vec3 velocity = particle.velocity_stretch.xyz;
        float speed = length(velocity);
        up = speed > 1e-4 ? velocity / speed : camera_up;
        vec3 side = cross(up, camera_ubo.camera_pos.xyz - center);
        float side_length = length(side);
        // flying at the camera: nothing to turn around
        right = side_length > 1e-5 ? side / side_length : camera_right;
        half_height += 0.5 * speed * particle.velocity_stretch.w;
    }
    else
    {
        float c = cos(particle.shape.x);
        float s = sin(particle.shape.x);
        right = camera_right * c + camera_up * s;
        up = camera_up * c - camera_right * s;
    }

    vec3 world = center + right * (corner.x * half_width) + up * (corner.y * half_height);
    vec4 view = camera_ubo.view * vec4(world, 1.0);
    gl_Position = camera_ubo.proj * view;

    v_uv = vec2(corner.x, -corner.y) * 0.5 + 0.5;
    if ((particle.info.z & PARTICLE_MIRRORED) != 0u)
        v_uv.x = 1.0 - v_uv.x;
    v_color = particle.color;
    if ((particle.info.z & PARTICLE_LIT) != 0u)
        v_color.rgb *= scene_light(center);
    v_view_depth = -view.z;
    v_particle = index;
}
