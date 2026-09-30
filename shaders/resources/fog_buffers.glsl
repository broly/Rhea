#ifndef RESOURCES_FOG_BUFFERS
#define RESOURCES_FOG_BUFFERS

// Fog of the frame marched at a fraction of the screen resolution (SkyRenderer, written by fog_march.frag),
// upsampled over the frame by fog.frag:
//   u_fog          rgb: light the fog scatters towards the eye, a: fraction of what is behind it that gets through
//   u_fog_history  u_fog of the previous frame

#ifndef SET_FOG_BUFFERS
    #define SET_FOG_BUFFERS 0
    #error "SET_FOG_BUFFERS definition is missing. Provide this resource: fog_buffers"
#endif
#ifndef BINDING_SAMPLER_FOG
    #define BINDING_SAMPLER_FOG 0
    #error "BINDING_SAMPLER_FOG definition is missing. Provide this resource: fog_buffers"
#endif
#ifndef BINDING_SAMPLER_FOG_HISTORY
    #define BINDING_SAMPLER_FOG_HISTORY 0
    #error "BINDING_SAMPLER_FOG_HISTORY definition is missing. Provide this resource: fog_buffers"
#endif

layout(set = SET_FOG_BUFFERS, binding = BINDING_SAMPLER_FOG)
uniform sampler2D u_fog;

layout(set = SET_FOG_BUFFERS, binding = BINDING_SAMPLER_FOG_HISTORY)
uniform sampler2D u_fog_history;

// The screen pixel a texel of the fog buffer was marched for: its view ray, its depth.
// scale: screen pixels per texel, per axis
ivec2 fog_texel_pixel(ivec2 texel, int scale, ivec2 screen)
{
    return min(texel * scale + scale / 2, screen - 1);
}

#endif // RESOURCES_FOG_BUFFERS
