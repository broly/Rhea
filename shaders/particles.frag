#version 450

#extension GL_EXT_nonuniform_qualifier : enable

// Sprites of the particle systems (ParticleRenderer, pass "Particles"), blended over the lit frame far to near
// with premultiplied alpha: a covering particle (smoke) writes its color x opacity and hides that much of what
// is behind it, a glowing one (fire) writes the same color and hides nothing.

#include "resources/camera.glsl"
#include "resources/gbuffer.glsl"
#include "resources/particles.glsl"

#ifndef SET_TEXTURES
    #define SET_TEXTURES 0
    #error "SET_TEXTURES definition is missing. Provide this resource: textures"
#endif
#ifndef BINDING_ARRAY_TEXTURES
    #define BINDING_ARRAY_TEXTURES 0
    #error "BINDING_ARRAY_TEXTURES definition is missing. Provide this resource: textures"
#endif

layout(set = SET_TEXTURES, binding = BINDING_ARRAY_TEXTURES)
uniform sampler2D u_textures_array[];

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in float v_view_depth;
layout(location = 3) flat in uint v_particle;

// blended: color = rgb + frame * (1 - a)
layout(location = 0) out vec4 out_color;

// m in front of the near plane over which a particle fades in: it does not pop when the camera passes through
const float NEAR_FADE = 0.3;

// One frame of a grid of frames; uv over the sprite, dx / dy its screen derivatives
vec4 sample_frame(uint texture_index, uint frame, uvec2 grid, vec2 uv, vec2 dx, vec2 dy)
{
    vec2 cells = vec2(grid);
    vec2 cell = vec2(frame % grid.x, frame / grid.x);
    // half a texel away from the neighbour frames
    vec2 border = 0.5 * cells / vec2(textureSize(u_textures_array[nonuniformEXT(texture_index)], 0));
    vec2 inside = clamp(uv, border, 1.0 - border);
    return textureGrad(u_textures_array[nonuniformEXT(texture_index)], (cell + inside) / cells, dx / cells, dy / cells);
}

void main()
{
    GPUParticle particle = u_particles.particles[v_particle];
    uint texture_index = particle.info.x;
    uint flags = particle.info.z;

    vec2 dx = dFdx(v_uv);
    vec2 dy = dFdy(v_uv);

    vec4 texel;
    if (texture_index == 0u)
    {
        // no texture: a soft round dot
        float falloff = clamp(1.0 - length(v_uv * 2.0 - 1.0), 0.0, 1.0);
        texel = vec4(1.0, 1.0, 1.0, falloff * falloff);
    }
    else
    {
        uvec2 grid = uvec2(particle.info.y & 0xffffu, particle.info.y >> 16);
        uint count = grid.x * grid.y;
        float frame = max(particle.shape.y, 0.0);
        uint first = min(uint(frame), count - 1u);
        texel = sample_frame(texture_index, first, grid, v_uv, dx, dy);
        if ((flags & PARTICLE_SMOOTH) != 0u)
        {
            uint next = first + 1u;
            if (next >= count)
                next = (flags & PARTICLE_LOOP) != 0u ? 0u : count - 1u;
            texel = mix(texel, sample_frame(texture_index, next, grid, v_uv, dx, dy), fract(frame));
        }
        // color textures are stored sRGB (as geometry.frag reads base colors)
        texel.rgb = pow(texel.rgb, vec3(2.2));
    }

    vec3 color = texel.rgb * v_color.rgb;
    float alpha = texel.a * v_color.a;

    // soft particles: no hard line where the sprite cuts into the scene
    ivec2 pixel = min(ivec2(gl_FragCoord.xy), textureSize(u_gbuffer[GBUFFER_SLOT_LINEAR_DEPTH], 0) - 1);
    float scene_depth = texelFetch(u_gbuffer[GBUFFER_SLOT_LINEAR_DEPTH], pixel, 0).r;
    float soft = particle.shape.z;
    // depth 0: the sky
    if (soft > 0.0 && scene_depth > 0.0)
        alpha *= clamp((scene_depth - v_view_depth) / soft, 0.0, 1.0);
    alpha *= clamp((v_view_depth - camera_ubo.near) / NEAR_FADE, 0.0, 1.0);

    out_color = vec4(color * alpha, alpha * (1.0 - particle.shape.w));
}
