module gpu_profile;

import <cstdint>;
import <string>;
import <string_view>;
import <vector>;
import <algorithm>;
import render;

namespace gpuprof
{
    void init(RenderBackend* backend)
    {
        auto& c = ctx();
        c.backend = backend;
        c.timestamp_period_ns = backend->get_timestamp_period_ns();
        for (auto& f : c.frames)
        {
            f.pool = backend->create_timestamp_pool(kMaxPassesPerFrame * 2);
            f.in_use = false;
            f.pass_names.clear();
        }
        c.initialized = true;
    }

    void shutdown()
    {
        auto& c = ctx();
        if (!c.initialized) return;
        for (auto& f : c.frames)
            c.backend->destroy_timestamp_pool(f.pool);
        c.initialized = false;
    }

    // Drain finished results from a pool that has cycled back around.
    static void collect_frame(uint32_t frame_in_flight)
    {
        auto& c = ctx();
        FramePool& fp = c.frames[frame_in_flight];
        if (!fp.in_use) return;

        const uint32_t pairs = static_cast<uint32_t>(fp.pass_names.size());
        if (pairs == 0) { fp.in_use = false; return; }

        std::vector<uint64_t> raw(pairs * 2, 0);
        const bool ok = c.backend->read_timestamps(
            fp.pool, 0, pairs * 2, raw.data());

        if (ok)
        {
            for (uint32_t i = 0; i < pairs; ++i)
            {
                uint64_t t0 = raw[i * 2 + 0];
                uint64_t t1 = raw[i * 2 + 1];
                if (t1 <= t0) continue;
                double ms = double(t1 - t0) * c.timestamp_period_ns / 1.0e6;

                auto& r = c.results[fp.pass_names[i]];
                r.name = fp.pass_names[i];
                r.sample_count += 1;
                r.total_ms += ms;
                r.last_ms = ms;
                r.min_ms = std::min(r.min_ms, ms);
                r.max_ms = std::max(r.max_ms, ms);
            }
        }

        fp.in_use = false;
        fp.pass_names.clear();
    }

    void frame_begin(uint32_t frame_in_flight, RBCommandList cmd)
    {
        auto& c = ctx();
        if (!c.initialized || !c.enabled) return;

        c.current_frame  = frame_in_flight;
        c.next_query     = 0;
        c.open_pass_base = -1;

        FramePool& fp = c.frames[frame_in_flight];

        // This pool last recorded MAX_FRAMES_IN_FLIGHT frames ago -> GPU is done.
        collect_frame(frame_in_flight);

        c.backend->cmd_reset_timestamp_pool(cmd, fp.pool, kMaxPassesPerFrame * 2);
        fp.pass_names.clear();
        fp.in_use = false;
    }

    void frame_end()
    {
        // results are collected lazily next time this frame index comes around.
    }

    void pass_begin(RBCommandList cmd, std::string_view pass_name)
    {
        auto& c = ctx();
        if (!c.initialized || !c.enabled) return;

        if (c.next_query + 2 > kMaxPassesPerFrame * 2)
        {
            c.open_pass_base = -1;   // out of budget this frame; skip
            return;
        }

        uint32_t base = c.next_query;
        c.next_query += 2;
        c.open_pass_base = int32_t(base);

        FramePool& fp = c.frames[c.current_frame];
        fp.pass_names.emplace_back(pass_name);
        fp.in_use = true;

        // begin -> top of pipe (before the work starts)
        c.backend->cmd_write_timestamp(cmd, fp.pool, base, /*bottom_of_pipe=*/false);
    }

    void pass_end(RBCommandList cmd)
    {
        auto& c = ctx();
        if (!c.initialized || !c.enabled) return;
        if (c.open_pass_base < 0) return;

        FramePool& fp = c.frames[c.current_frame];
        // end -> bottom of pipe (after all stages of the pass complete)
        c.backend->cmd_write_timestamp(
            cmd, fp.pool, uint32_t(c.open_pass_base) + 1, /*bottom_of_pipe=*/true);
        c.open_pass_base = -1;
    }

    void dump()
    {
        auto& c = ctx();
        auto path = paths::get_project_path() / "gpu_profiling_dump.txt";
        std::ofstream file(path.c_str());
        if (!file.is_open())
        {
            std::cerr << "Failed to open gpu_profiling_dump.txt\n";
            return;
        }

        std::vector<PassResult> rows;
        rows.reserve(c.results.size());
        for (auto& [_, r] : c.results) rows.push_back(r);
        std::sort(rows.begin(), rows.end(),
            [](const PassResult& a, const PassResult& b){ return a.total_ms > b.total_ms; });

        double grand_total = 0.0;
        for (auto& r : rows) grand_total += r.total_ms;

        file << "GPU Pass Timing (sorted by total GPU time)\n";
        file << "timestamp_period applied; times in ms\n\n";
        file << "  pass                          samples     avg      min      max     total    %\n";
        for (auto& r : rows)
        {
            double avg = r.sample_count ? r.total_ms / double(r.sample_count) : 0.0;
            double pct = grand_total > 0.0 ? 100.0 * r.total_ms / grand_total : 0.0;
            char line[320];
            std::snprintf(line, sizeof(line),
                "  %-28s %7llu %8.4f %8.4f %8.4f %9.3f %5.1f\n",
                r.name.c_str(), (unsigned long long)r.sample_count,
                avg, (r.min_ms >= 1e29 ? 0.0 : r.min_ms), r.max_ms, r.total_ms, pct);
            file << line;
        }
        file.close();
        std::cout << "GPU profiling data dumped to gpu_profiling_dump.txt\n";
    }
}
