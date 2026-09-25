export module debug_draw;

import std.compat;
import glm;

// Immediate mode debug lines in world space, from any thread. The renderer draws them in the debug
// overlay pass of the next frame (GenericRenderGraph::draw_debug_overlay) and drops them, unless
// they were given a lifetime.
//
//     debug_draw::line(a, b, {1, 0, 0, 1});
//     debug_draw::sphere(hit.position, 0.05f, {0, 1, 0, 1}, {.seconds = 2.f});
export namespace debug_draw
{
    struct Options
    {
        float seconds = 0.0f;       // 0: one frame
        bool depth_test = true;     // false: drawn on top of the scene
    };

    struct Line
    {
        glm::vec3 a;
        glm::vec3 b;
        glm::vec4 color;
    };

    void line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, const Options& options = {});
    void lines(std::span<const Line> lines, const Options& options = {});

    void arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, const Options& options = {});
    void cross(const glm::vec3& center, float size, const glm::vec4& color, const Options& options = {});
    void box(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color, const Options& options = {});
    void sphere(const glm::vec3& center, float radius, const glm::vec4& color, const Options& options = {});

    // Lines to draw this frame, split by depth test. Called by the renderer once per frame.
    struct Frame
    {
        std::vector<Line> depth_tested;
        std::vector<Line> on_top;
    };
    void collect(Frame& out);
}
