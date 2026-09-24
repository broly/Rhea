#ifndef CHARACTER_SHADING_MODELS
#define CHARACTER_SHADING_MODELS

// Shading model id is stored in g_geometry_normal.a (RGBA8).
//   0   - cleared pixel (no geometry)          -> legacy lighting
//   255 - legacy PBR model (writes alpha 1.0)  -> legacy lighting
// Character models store extra data in g_emissive (rgba) and AO in g_world_normal.a:
//   default lit: emissive.rgb = emissive,        emissive.a = metallic
//   skin:        emissive.rgb = subsurface color, emissive.a = scatter
//   hair:        emissive.rgb = tangent * 0.5 + 0.5, emissive.a = scatter

#define SHADING_MODEL_ID_CLEAR       0u
#define SHADING_MODEL_ID_DEFAULT_LIT 1u
#define SHADING_MODEL_ID_SKIN        2u
#define SHADING_MODEL_ID_HAIR        3u
#define SHADING_MODEL_ID_LEGACY      255u

float encode_shading_model(uint id)
{
    return float(id) / 255.0;
}

uint decode_shading_model(float value)
{
    return uint(value * 255.0 + 0.5);
}

#endif // CHARACTER_SHADING_MODELS
