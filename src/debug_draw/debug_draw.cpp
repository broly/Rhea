module debug_draw;

import std.compat;
import glm;

namespace debug_draw
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        struct TimedLine
        {
            Line line;
            Clock::time_point expires;
            bool depth_test;
        };

        struct State
        {
            std::mutex mutex;
            Frame frame;
            std::vector<TimedLine> timed;
        };

        State& state()
        {
            static State s;
            return s;
        }

        void add(std::span<const Line> lines, const Options& options)
        {
            State& s = state();
            std::lock_guard lock(s.mutex);
            if (options.seconds > 0.0f)
            {
                const auto expires = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<float>(options.seconds));
                for (const Line& l : lines)
                    s.timed.push_back({ l, expires, options.depth_test });
                return;
            }
            auto& target = options.depth_test ? s.frame.depth_tested : s.frame.on_top;
            target.insert(target.end(), lines.begin(), lines.end());
        }

        // any vector perpendicular to n
        glm::vec3 perpendicular(const glm::vec3& n)
        {
            const glm::vec3 other = std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            return glm::normalize(glm::cross(n, other));
        }
    }

    void line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, const Options& options)
    {
        const Line l{ a, b, color };
        add(std::span(&l, 1), options);
    }

    void lines(std::span<const Line> in_lines, const Options& options)
    {
        add(in_lines, options);
    }

    void arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, const Options& options)
    {
        const glm::vec3 delta = to - from;
        const float length = glm::length(delta);
        if (length < 1e-6f)
            return;

        const glm::vec3 dir = delta / length;
        const glm::vec3 side = perpendicular(dir);
        const glm::vec3 up = glm::cross(dir, side);
        const float head = std::min(0.25f * length, 0.1f);
        const glm::vec3 base = to - dir * head;

        const Line l[] = {
            { from, to, color },
            { to, base + side * head * 0.5f, color },
            { to, base - side * head * 0.5f, color },
            { to, base + up * head * 0.5f, color },
            { to, base - up * head * 0.5f, color },
        };
        add(l, options);
    }

    void cross(const glm::vec3& center, float size, const glm::vec4& color, const Options& options)
    {
        const float h = size * 0.5f;
        const Line l[] = {
            { center - glm::vec3(h, 0, 0), center + glm::vec3(h, 0, 0), color },
            { center - glm::vec3(0, h, 0), center + glm::vec3(0, h, 0), color },
            { center - glm::vec3(0, 0, h), center + glm::vec3(0, 0, h), color },
        };
        add(l, options);
    }

    void box(const glm::vec3& mn, const glm::vec3& mx, const glm::vec4& color, const Options& options)
    {
        const glm::vec3 c[8] = {
            { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mn.x, mx.y, mn.z }, { mx.x, mx.y, mn.z },
            { mn.x, mn.y, mx.z }, { mx.x, mn.y, mx.z }, { mn.x, mx.y, mx.z }, { mx.x, mx.y, mx.z },
        };
        const Line l[] = {
            { c[0], c[1], color }, { c[1], c[3], color }, { c[3], c[2], color }, { c[2], c[0], color },
            { c[4], c[5], color }, { c[5], c[7], color }, { c[7], c[6], color }, { c[6], c[4], color },
            { c[0], c[4], color }, { c[1], c[5], color }, { c[2], c[6], color }, { c[3], c[7], color },
        };
        add(l, options);
    }

    void sphere(const glm::vec3& center, float radius, const glm::vec4& color, const Options& options)
    {
        constexpr int segments = 16;
        std::array<Line, segments * 3> l;
        for (int i = 0; i < segments; ++i)
        {
            const float a0 = 2.0f * std::numbers::pi_v<float> * (float)i / segments;
            const float a1 = 2.0f * std::numbers::pi_v<float> * (float)(i + 1) / segments;
            const float c0 = std::cos(a0) * radius, s0 = std::sin(a0) * radius;
            const float c1 = std::cos(a1) * radius, s1 = std::sin(a1) * radius;
            l[i * 3 + 0] = { center + glm::vec3(c0, s0, 0), center + glm::vec3(c1, s1, 0), color };
            l[i * 3 + 1] = { center + glm::vec3(c0, 0, s0), center + glm::vec3(c1, 0, s1), color };
            l[i * 3 + 2] = { center + glm::vec3(0, c0, s0), center + glm::vec3(0, c1, s1), color };
        }
        add(l, options);
    }

    void collect(Frame& out)
    {
        out.depth_tested.clear();
        out.on_top.clear();

        State& s = state();
        std::lock_guard lock(s.mutex);
        std::swap(out, s.frame);

        const auto now = Clock::now();
        std::erase_if(s.timed, [now] (const TimedLine& t) { return t.expires <= now; });
        for (const TimedLine& t : s.timed)
            (t.depth_test ? out.depth_tested : out.on_top).push_back(t.line);
    }
}
