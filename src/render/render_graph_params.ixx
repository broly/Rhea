export module render:rg_params;

import std.compat;

import name;
import glm;

export struct RenderGraphParameters
{
    uint32_t render_iter_id = 0;
    uint32_t num_runs = 0;
    uint32_t output_frame_id = 0;
    std::map<Name, int> int_params;
    std::map<Name, bool> bool_params;
    std::map<Name, glm::vec3> vec3_params;
    // draw the debug UI over the swapchain image at the end of the graph
    bool draw_ui_overlay = false;
    
    int get_int(Name param, int default_value = 0) const
    {
        auto it = int_params.find(param);
        if (it != int_params.end())
            return it->second;
        return default_value;
    }
};

// Renderer int param "sync_debug": serializes the GPU to bisect synchronization bugs (live, from the debug UI)
//   1  full barrier at the start of every graph: no overlap with the previous frames in flight
//   2  + full barrier before every pass: no overlap between passes (missing / wrong graph barriers)
//   3  + wait idle after every submit: the CPU never writes memory a frame in flight still reads
export namespace SyncDebug
{
    inline constexpr const char* param = "sync_debug";
    inline constexpr int frame_barrier = 1;
    inline constexpr int pass_barriers = 2;
    inline constexpr int wait_idle = 3;
}
