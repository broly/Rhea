export module gpu_profile;

import <cstdint>;
import <string>;
import <string_view>;
import <vector>;
import <unordered_map>;
import <array>;
import <fstream>;
import <iostream>;
import <algorithm>;
import render;        // RenderBackend (abstract), RB* handle types
import paths;

// =============================================================================
// GPU pass profiler (timestamp queries).
//
// Profiles render-graph PASSES directly: the execute loop calls
// gpuprof::pass_begin(cmd, name) before a pass and gpuprof::pass_end(cmd) after.
// No RAII -- passes already have an explicit begin/end in the loop, so bracketing
// them by hand is clearer than a scope object.
//
// GPU timing is ASYNCHRONOUS: cmd_write_timestamp only RECORDS a "write the GPU
// clock here" command. The value isn't readable until the GPU finishes the
// buffer -- several frames later. So we keep one timestamp pool PER
// FRAME-IN-FLIGHT (a ring): we write into the current frame's pool, and only read
// a pool back once its frame index has cycled all the way around (GPU guaranteed
// done). Results accumulate per pass-name for dumping.
//
// Runtime-toggleable via set_enabled(); dumps to gpu_profiling_dump.txt.
// Talks ONLY to the abstract RenderBackend; Vulkan query mechanics live in
// VkRenderBackend.
// =============================================================================

export namespace gpuprof
{
    inline constexpr uint32_t kMaxPassesPerFrame = 128;   // begin+end => 2 queries each

    struct PassResult
    {
        std::string name;
        uint64_t    sample_count = 0;
        double      total_ms     = 0.0;
        double      last_ms      = 0.0;
        double      min_ms       = 1e30;
        double      max_ms       = 0.0;
    };

    struct FramePool
    {
        RBQueryPool              pool{};
        bool                     in_use = false;
        std::vector<std::string> pass_names;   // index i -> queries (2i, 2i+1)
    };

    struct Context
    {
        RenderBackend* backend = nullptr;
        double  timestamp_period_ns = 1.0;
        bool    enabled = false;
        bool    initialized = false;

        std::array<FramePool, kRenderMaxFramesInFlight> frames{};
        uint32_t current_frame = 0;
        uint32_t next_query = 0;
        int32_t  open_pass_base = -1;   // query base of the currently-open pass (-1 = none)

        std::unordered_map<std::string, PassResult> results;
    };

    inline Context& ctx()
    {
        static Context c;
        return c;
    }

    // ---- runtime toggle -----------------------------------------------------
    export void set_enabled(bool v) { ctx().enabled = v; }
    export bool is_enabled()        { return ctx().enabled; }
    export void clear_results()     { ctx().results.clear(); }

    // ---- lifecycle ----------------------------------------------------------
    export void init(RenderBackend* backend);
    export void shutdown();

    // ---- frame boundaries (called by the render graph each frame) -----------
    export void frame_begin(uint32_t frame_in_flight, RBCommandList cmd);
    export void frame_end();

    // ---- pass boundaries (called around pass.execute in the loop) -----------
    export void pass_begin(RBCommandList cmd, std::string_view pass_name);
    export void pass_end(RBCommandList cmd);

    // ---- dump ---------------------------------------------------------------
    export void dump();
}
