export module profile;
import <source_location>;
import <chrono>;
import <unordered_map>;
import <iostream>;
import <fstream>;
import <numeric>;
import paths;

using Clock = std::chrono::system_clock;

#define FORCEINLINE __forceinline

namespace prof
{
    struct ProfilingData
    {
        std::vector<std::chrono::nanoseconds> samples;
        std::chrono::nanoseconds frame_accum;
        std::vector<std::chrono::nanoseconds> samples_accum_per_frame;
        std::vector<float> calls_per_frame;
        uint32_t calls_accum_for_frame;
        
    };

    struct ProfilingDataInstance
    {
        std::unordered_map<const char*, ProfilingData> reports; 
        std::chrono::time_point<Clock> frame_start_point;
    };

    ProfilingDataInstance& get_profiling_instance() noexcept
    {
        static ProfilingDataInstance instance;
        return instance;
    }

    export bool& get_is_profiling()
    {
        static bool is_profiling = false;
        return is_profiling;
    }

    export void set_is_profiling(bool value)
    {
        get_is_profiling() = value;
    }

    export bool is_profiling()
    {
        return get_is_profiling();
    }
    
    FORCEINLINE void report_profiling_data(const char* name, std::chrono::nanoseconds sample) noexcept
    {
        auto& data = get_profiling_instance().reports[name];
        data.samples.push_back(sample); 
        data.frame_accum += sample;
        data.calls_accum_for_frame += 1;
    }
    
    export FORCEINLINE void frame_start()
    {
        
    }
    
    export FORCEINLINE void frame_end()
    {
        auto& instance = get_profiling_instance();
        for (auto& [name, data] : instance.reports)
        {
            data.samples_accum_per_frame.push_back(data.frame_accum);
            data.calls_per_frame.push_back(data.calls_accum_for_frame);
            data.calls_accum_for_frame = 0;
            data.frame_accum = std::chrono::nanoseconds(0);
        }
    }
    
    FORCEINLINE std::string format_duration(std::chrono::nanoseconds ns)
    {
        using namespace std::chrono;
        double value = static_cast<double>(ns.count());

        if (value < 1'000.0)                  return std::to_string(static_cast<long long>(value)) + " ns";
        value /= 1'000.0;
        if (value < 1'000.0)                  return std::to_string(static_cast<long long>(value)) + " µs";
        value /= 1'000.0;
        if (value < 1'000.0)                  return std::to_string(static_cast<long long>(value)) + " ms";
        value /= 1'000.0;
        return std::to_string(value) + " s";
    }

    export void dump()
    {
        auto& instance = get_profiling_instance();
        
        
        auto path = paths::get_project_path() / "profiling_dump.txt";
        std::ofstream file(path.c_str());
        if (!file.is_open())
        {
            std::cerr << "Failed to open profiling_dump.txt for writing!\n";
            return;
        }

        file << "Profiling Dump:\n";

        for (auto& [name, data] : instance.reports)
        {
            if (data.samples.empty())
                continue;

            auto max = *std::max_element(data.samples.begin(), data.samples.end());

            auto total = std::accumulate(
                data.samples.begin(),
                data.samples.end(),
                std::chrono::nanoseconds(0)
            );
            auto avg = total / static_cast<long long>(data.samples.size());
            double avg_sec = std::chrono::duration<double>(avg).count();
            double fps = avg_sec > 0.0 ? 1.0 / avg_sec : 0.0;
            
            auto max_per_frame = *std::max_element(data.samples_accum_per_frame.begin(), data.samples_accum_per_frame.end());

            auto total_per_frame = std::accumulate(
                data.samples_accum_per_frame.begin(),
                data.samples_accum_per_frame.end(),
                std::chrono::nanoseconds(0)
            );
            auto avg_per_frame = total_per_frame / static_cast<long long>(data.samples_accum_per_frame.size());
            double avg_sec_per_frame = std::chrono::duration<double>(avg_per_frame).count();
            double fps_per_frame = avg_sec_per_frame > 0.0 ? 1.0 / avg_sec_per_frame : 0.0;
            
            auto total_calls = std::accumulate(
                data.calls_per_frame.begin(),
                data.calls_per_frame.end(),
                0.f
            );
            const float avg_calls = total_calls / static_cast<float>(data.calls_per_frame.size());

            file << "Function: " << name
                    << ", Calls: " << data.samples.size()
                    << ", Max: " << format_duration(max)
                    << ", Avg: " << format_duration(avg)
                    << ", FPS: " << std::fixed << std::setprecision(2) << fps 
                    << ", Max (per_frame): " << format_duration(max_per_frame)
                    << ", Avg (per_frame): " << format_duration(avg_per_frame)
                    << ", Calls (per_frame): " << std::fixed << std::setprecision(2) << avg_calls
                    << ", FPS (per_frame): " << std::fixed << std::setprecision(2) << fps_per_frame << "\n";
        }

        file.close();
        std::cout << "Profiling data dumped to profiling_dump.txt\n";
    }
}

export struct Profile
{
    FORCEINLINE Profile(
        const char* in_name = nullptr, 
        const std::source_location& sl = std::source_location::current()
        ) noexcept
        : name(in_name ? in_name : sl.function_name())
    {
        start_point = Clock::now();
    }
    
    FORCEINLINE ~Profile() noexcept
    {
        auto end_point = Clock::now();
        auto delta = end_point - start_point;
        if (prof::is_profiling())
            prof::report_profiling_data(name, delta);
    }
    std::chrono::time_point<Clock> start_point;
    const char* name;
};
