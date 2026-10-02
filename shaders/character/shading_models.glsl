#ifndef CHARACTER_SHADING_MODELS
#define CHARACTER_SHADING_MODELS

// Shading model id is stored in g_geometry_normal.a (RGBA8).
//   0   - cleared pixel (no geometry)          -> legacy lighting
//   255 - legacy PBR model (writes alpha 1.0)  -> legacy lighting
// Character models store extra data in g_emissive (rgba) and AO in g_world_normal.a:
//   default lit: emissive.rgb = emissive,        emissive.a = metallic
//   skin:        emissive.rgb = subsurface color, emissive.a = scatter
//   hair:        emissive.rgb = tangent * 0.5 + 0.5, emissive.a = scatter
// Foliage (foliage.frag) is lit by the legacy path plus the light passing through the leaves:
//   foliage:     emissive.rgb = transmission color (albedo x tint x strength), emissive.a = metallic
//
// The byte: bits 0..4 the id, bits 5..6 the JPEG mask level of the instance (0..3: MeshRenderer::effect.a, a
// creature hit by a weapon) that jpeg_hit_mask.comp covers whole. 255 (legacy) has no level.

#define SHADING_MODEL_ID_CLEAR       0u
#define SHADING_MODEL_ID_DEFAULT_LIT 1u
#define SHADING_MODEL_ID_SKIN        2u
#define SHADING_MODEL_ID_HAIR        3u
#define SHADING_MODEL_ID_FOLIAGE     4u
#define SHADING_MODEL_ID_LEGACY      255u

float encode_shading_model(uint id)
{
    return float(id) / 255.0;
}

// jpeg: 0..1, the strength of the whole-body JPEG of the instance (quantized to 3 levels)
float encode_shading_model_jpeg(uint id, float jpeg)
{
    const uint level = uint(clamp(jpeg, 0.0, 1.0) * 3.0 + 0.5);
    return float(id | (level << 5u)) / 255.0;
}

uint decode_shading_model(float value)
{
    const uint byte_value = uint(value * 255.0 + 0.5);
    return byte_value == SHADING_MODEL_ID_LEGACY ? byte_value : (byte_value & 31u);
}

// 0..3
uint decode_jpeg_mask_level(float value)
{
    const uint byte_value = uint(value * 255.0 + 0.5);
    return byte_value == SHADING_MODEL_ID_LEGACY ? 0u : ((byte_value >> 5u) & 3u);
}

#endif // CHARACTER_SHADING_MODELS
