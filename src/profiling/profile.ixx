export module profile;

import <source_location>;
import <chrono>;
import <unordered_map>;
import <string_view>;
import <iostream>;
import <fstream>;
import <vector>;
import paths;

using Clock = std::chrono::high_resolution_clock;

#define FORCEINLINE __forceinline

export namespace prof
{
    // ------------------------------------------------------------
    // Hierarchical node
    // ------------------------------------------------------------
    struct ProfileNode
    {
        std::string_view name;

        uint64_t call_count = 0;

        std::chrono::nanoseconds inclusive_time{0};
        std::chrono::nanoseconds exclusive_time{0};

        std::unordered_map<std::string_view, ProfileNode*> children;
        ProfileNode* parent = nullptr;
    };

    // ------------------------------------------------------------
    // Global profiling context
    // ------------------------------------------------------------
    struct ProfilingContext
    {
        ProfileNode root{ .name = "ROOT" };
    };

    inline ProfilingContext& get_context()
    {
        static ProfilingContext ctx;
        return ctx;
    }

    // ------------------------------------------------------------
    // Global enable flag
    // ------------------------------------------------------------
    export bool& get_is_profiling()
    {
        static bool enabled = false;
        return enabled;
    }

    export void set_is_profiling(bool value)
    {
        get_is_profiling() = value;
    }

    export bool is_profiling()
    {
        return get_is_profiling();
    }

    // ------------------------------------------------------------
    // Thread-local current node (stack top)
    // ------------------------------------------------------------
    thread_local ProfileNode* tls_current_node = nullptr;

    // ------------------------------------------------------------
    // Frame control
    // ------------------------------------------------------------
    export FORCEINLINE void frame_start()
    {
        auto& ctx = get_context();
        tls_current_node = &ctx.root;
    }

    export FORCEINLINE void frame_end()
    {
        
    }

    // ------------------------------------------------------------
    // Formatting
    // ------------------------------------------------------------
    FORCEINLINE std::string format_duration(std::chrono::nanoseconds ns)
    {
        using namespace std::chrono;

        double value = static_cast<double>(ns.count());

        if (value < 1'000.0)   return std::to_string((long long)value) + " ns";
        value /= 1'000.0;
        if (value < 1'000.0)   return std::to_string((long long)value) + " µs";
        value /= 1'000.0;
        if (value < 1'000.0)   return std::to_string((long long)value) + " ms";
        value /= 1'000.0;
        return std::to_string(value) + " s";
    }

    // ------------------------------------------------------------
    // Dump
    // ------------------------------------------------------------
    void dump_node(std::ofstream& file, ProfileNode* node, int depth)
    {
        if (node->name != "ROOT")
        {
            for (int i = 0; i < depth; ++i)
                file << "  ";

            file << node->name
                 << " | calls: " << node->call_count
                 << " | incl: " << format_duration(node->inclusive_time)
                 << " | excl: " << format_duration(node->exclusive_time)
                 << "\n";
        }

        for (auto& [_, child] : node->children)
            dump_node(file, child, depth + 1);
    }

    export void dump()
    {
        auto path = paths::get_project_path() / "profiling_dump.txt";
        std::ofstream file(path.c_str());

        if (!file.is_open())
        {
            std::cerr << "Failed to open profiling_dump.txt\n";
            return;
        }

        file << "Hierarchical Profiling Dump\n\n";
        dump_node(file, &get_context().root, 0);
        file.close();

        std::cout << "Profiling data dumped to profiling_dump.txt\n";
    }
}

// ------------------------------------------------------------
// RAII scope
// ------------------------------------------------------------
export struct Profile
{
    FORCEINLINE Profile(
        const char* in_name = nullptr,
        const std::source_location& sl = std::source_location::current()
    ) noexcept
        : name(in_name ? in_name : sl.function_name())
    {
        if (!prof::is_profiling())
            return;

        auto& ctx = prof::get_context();

        if (!prof::tls_current_node)
            prof::tls_current_node = &ctx.root;

        prof::ProfileNode* parent = prof::tls_current_node;

        auto it = parent->children.find(name);
        if (it == parent->children.end())
        {
            auto* node = new prof::ProfileNode{};
            node->name = name;
            node->parent = parent;
            parent->children[name] = node;
            current = node;
        }
        else
        {
            current = it->second;
        }

        current->call_count++;
        prof::tls_current_node = current;

        start = Clock::now();
    }

    FORCEINLINE ~Profile() noexcept
    {
        if (!current)
            return;

        auto end = Clock::now();
        auto delta = end - start;

        current->inclusive_time += delta;
        current->exclusive_time += delta;

        if (current->parent)
            current->parent->exclusive_time -= delta;

        prof::tls_current_node = current->parent;
    }

private:
    std::chrono::time_point<Clock> start;
    std::string_view name;
    prof::ProfileNode* current = nullptr;
};
