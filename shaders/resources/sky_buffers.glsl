#ifndef RESOURCES_SKY_BUFFERS
#define RESOURCES_SKY_BUFFERS

// Sky of the frame marched at a fraction of the screen resolution (SkyRenderer, sky_march.comp writes them
// through resources/sky_targets.glsl), upsampled by sky.frag:
//   u_atmosphere      rgb: sun light scattered by the air along the view ray, a: what the air lets through
//                     of the space behind it (night sky, stars), as one number
//   u_clouds          rgb: light the clouds scatter towards the eye, a: fraction of the sky behind them
//                     that gets through
//   u_clouds_history  u_clouds of the previous frame
// a < 0 in any of them: the texel was not computed, no sky is visible in its part of the screen.

#ifndef SET_SKY_BUFFERS
    #define SET_SKY_BUFFERS 0
    #error "SET_SKY_BUFFERS definition is missing. Provide this resource: sky_buffers"
#endif
#ifndef BINDING_SAMPLER_ATMOSPHERE
    #define BINDING_SAMPLER_ATMOSPHERE 0
    #error "BINDING_SAMPLER_ATMOSPHERE definition is missing. Provide this resource: sky_buffers"
#endif
#ifndef BINDING_SAMPLER_CLOUDS
    #define BINDING_SAMPLER_CLOUDS 0
    #error "BINDING_SAMPLER_CLOUDS definition is missing. Provide this resource: sky_buffers"
#endif
#ifndef BINDING_SAMPLER_CLOUDS_HISTORY
    #define BINDING_SAMPLER_CLOUDS_HISTORY 0
    #error "BINDING_SAMPLER_CLOUDS_HISTORY definition is missing. Provide this resource: sky_buffers"
#endif

layout(set = SET_SKY_BUFFERS, binding = BINDING_SAMPLER_ATMOSPHERE)
uniform sampler2D u_atmosphere;

layout(set = SET_SKY_BUFFERS, binding = BINDING_SAMPLER_CLOUDS)
uniform sampler2D u_clouds;

layout(set = SET_SKY_BUFFERS, binding = BINDING_SAMPLER_CLOUDS_HISTORY)
uniform sampler2D u_clouds_history;

// Bilinear read of a sky buffer over the texels that were computed. False when none of the four was.
bool sky_buffer_sample(sampler2D sky_buffer, vec2 uv, out vec4 value)
{
    ivec2 size = textureSize(sky_buffer, 0);
    vec2 position = uv * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(position));
    vec2 f = position - vec2(base);

    vec4 sum = vec4(0.0);
    float weight_sum = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        ivec2 offset = ivec2(i & 1, i >> 1);
        vec4 texel = texelFetch(sky_buffer, clamp(base + offset, ivec2(0), size - 1), 0);
        float weight = (offset.x == 0 ? 1.0 - f.x : f.x) * (offset.y == 0 ? 1.0 - f.y : f.y);
        if (texel.a >= 0.0)
        {
            sum += texel * weight;
            weight_sum += weight;
        }
    }

    value = sum / max(weight_sum, 1e-5);
    return weight_sum > 1e-4;
}

#endif // RESOURCES_SKY_BUFFERS
