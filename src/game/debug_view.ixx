export module game:debug_view;

import std.compat;



// What the tonemap pass outputs (renderer int param DebugViewParams::view_mode).
// Keep in sync with the VIEW_* constants in tonemap.slang
export enum class DebugViewMode : uint32_t
{
    LIT = 0,
    WIREFRAME,          // mesh edges over a dim clay shading
    LIT_WIREFRAME,      // mesh edges over the final image

    // raw gbuffer channels
    BASE_COLOR,
    ROUGHNESS,
    WORLD_NORMAL,
    VIEW_NORMAL,
    GEOMETRY_NORMAL,
    LINEAR_DEPTH,
    WORLD_POSITION,
    MOTION_VECTORS,
    EMISSIVE,

    COUNT
};

export constexpr DebugViewMode first_gbuffer_view_mode = DebugViewMode::BASE_COLOR;

export constexpr bool is_wireframe_view_mode(DebugViewMode mode)
{
    return mode == DebugViewMode::WIREFRAME || mode == DebugViewMode::LIT_WIREFRAME;
}

export constexpr bool is_gbuffer_view_mode(DebugViewMode mode)
{
    return mode >= first_gbuffer_view_mode && mode < DebugViewMode::COUNT;
}

export constexpr const char* get_debug_view_mode_name(DebugViewMode mode)
{
    switch (mode)
    {
        case DebugViewMode::LIT:             return "Lit";
        case DebugViewMode::WIREFRAME:       return "Wireframe";
        case DebugViewMode::LIT_WIREFRAME:   return "Lit + Wireframe";
        case DebugViewMode::BASE_COLOR:      return "GBuffer: Base Color";
        case DebugViewMode::ROUGHNESS:       return "GBuffer: Roughness";
        case DebugViewMode::WORLD_NORMAL:    return "GBuffer: World Normal";
        case DebugViewMode::VIEW_NORMAL:     return "GBuffer: View Normal";
        case DebugViewMode::GEOMETRY_NORMAL: return "GBuffer: Geometry Normal";
        case DebugViewMode::LINEAR_DEPTH:    return "GBuffer: Linear Depth";
        case DebugViewMode::WORLD_POSITION:  return "GBuffer: World Position";
        case DebugViewMode::MOTION_VECTORS:  return "GBuffer: Motion Vectors";
        case DebugViewMode::EMISSIVE:        return "GBuffer: Emissive";
        default:                             return "Unknown";
    }
}

// renderer int params that drive the debug views
export namespace DebugViewParams
{
    inline constexpr const char* view_mode = "view_mode";
    inline constexpr const char* show_skeleton = "show_skeleton";
}
