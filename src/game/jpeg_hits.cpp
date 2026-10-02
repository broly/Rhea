module game;

import :jpeg_hits;

import std.compat;

import glm;
import cvar;
import :jpeg;

namespace
{
    struct Hit
    {
        glm::vec3 center;
        float radius;
        float strength;     // at birth
        float lifetime;
        float age = 0.0f;
        float seed;

        float current_strength() const
        {
            return strength * std::clamp(1.0f - age / lifetime, 0.0f, 1.0f);
        }
    };

    std::vector<Hit> g_hits;
    uint32_t g_hit_counter = 0;
    std::optional<jpeg_hits::ShootRequest> g_shoot_request;

    float parse_float(cvar::Args args, size_t index, float fallback)
    {
        float value = fallback;
        if (index < args.size())
            std::from_chars(args[index].data(), args[index].data() + args[index].size(), value);
        return value;
    }

    cvar::Command cmd_shoot("render.jpeg.hits.shoot", "Hits the surface in the middle of the screen (JPEG spot)",
        [] (cvar::Args args)
        {
            g_shoot_request = jpeg_hits::ShootRequest{
                .radius = parse_float(args, 0, 0.0f),
                .strength = parse_float(args, 1, 1.0f),
                .lifetime = parse_float(args, 2, 0.0f),
            };
        },
        "[radius m] [strength 0..1] [lifetime s]");

    cvar::Command cmd_clear("render.jpeg.hits.clear", "Removes every JPEG spot",
        [] (cvar::Args) { jpeg_hits::clear(); });
}

void jpeg_hits::add(const glm::vec3& center, float radius, float strength, float lifetime)
{
    Hit hit{
        .center = center,
        .radius = radius > 0.0f ? radius : cv_jpeg_hits_radius.get(),
        .strength = std::clamp(strength, 0.0f, 1.0f),
        .lifetime = std::max(lifetime > 0.0f ? lifetime : cv_jpeg_hits_lifetime.get(), 0.01f),
        // golden ratio steps: neighbouring hits get unrelated ragged borders
        .seed = std::fmod((float)++g_hit_counter * 0.618034f, 1.0f) * 1000.0f,
    };
    if (g_hits.size() < max_hits)
    {
        g_hits.push_back(hit);
        return;
    }
    auto weakest = std::ranges::min_element(g_hits, {}, &Hit::current_strength);
    *weakest = hit;
}

void jpeg_hits::clear()
{
    g_hits.clear();
}

void jpeg_hits::tick(float delta_seconds)
{
    const float dt = std::clamp(delta_seconds, 0.0f, 0.25f);
    for (Hit& hit : g_hits)
        hit.age += dt;
    std::erase_if(g_hits, [] (const Hit& hit) { return hit.age >= hit.lifetime; });
}

bool jpeg_hits::any()
{
    return cv_jpeg_hits_enabled.get() && !g_hits.empty();
}

JpegHitsUBO jpeg_hits::build_ubo()
{
    JpegHitsUBO ubo{};
    uint32_t count = 0;
    for (const Hit& hit : g_hits)
    {
        const float strength = hit.current_strength();
        if (strength <= 0.0f)
            continue;
        ubo.spheres[count] = glm::vec4(hit.center, hit.radius);
        ubo.params[count] = glm::vec4(strength, hit.seed, 0.0f, 0.0f);
        ++count;
    }
    ubo.info = glm::uvec4(count, 0u, 0u, 0u);
    ubo.settings = glm::vec4(std::clamp(cv_jpeg_hits_edge.get(), 0.0f, 1.0f), 0.0f, 0.0f, 0.0f);
    return ubo;
}

JpegSettings jpeg_hits::chain_settings()
{
    return jpeg::clamped({
        .downscale = 1,
        .filter = JpegFilter::Nearest,
        .quality = cv_jpeg_hits_quality.get(),
        .chroma = cv_jpeg_hits_chroma.get(),
        .generations = cv_jpeg_hits_generations.get(),
        .sharpen = cv_jpeg_hits_sharpen.get(),
        .seed = g_hit_counter,
        .mask_quality = cv_jpeg_hits_quality_weak.get(),
    });
}

std::optional<jpeg_hits::ShootRequest> jpeg_hits::take_shoot_request()
{
    return std::exchange(g_shoot_request, std::nullopt);
}
