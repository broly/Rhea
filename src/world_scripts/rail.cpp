module rail;

import std.compat;
import ecs;

void Rail::start()
{
    active = true;
    accumulated_time = 0.0f;

    for (auto& [name, track] : samples)
    {
        std::ranges::sort(track, [] (const RailSample& a, const RailSample& b) {
            return a.timestamp_seconds < b.timestamp_seconds;
        });
    }
}

void Rail::tick(double dt)
{
    if (!active || samples.empty())
        return;

    const double delta = fixed_timestep ? timestep.value_or(0.0f) : dt;
    accumulated_time += float(delta * time_dilation);
    const float t = accumulated_time;

    bool all_done = true;

    for (auto& [name, track] : samples)
    {
        if (track.empty())
            continue;

        auto callback = on_tick.find(name);
        if (callback == on_tick.end() || !callback->second)
            continue;

        if (t < track.back().timestamp_seconds)
            all_done = false;

        if (track.size() == 1 || t >= track.back().timestamp_seconds)
        {
            const RailSample& last = track.back();
            callback->second(RailSampleData{ last.position, last.rotation, last.color });
            continue;
        }

        int segment = -1;
        for (int i = 0; i < (int)track.size() - 1; ++i)
        {
            if (t >= track[i].timestamp_seconds && t < track[i + 1].timestamp_seconds)
            {
                segment = i;
                break;
            }
        }
        if (segment == -1)
            continue;

        const RailSample& a = track[segment];
        const RailSample& b = track[segment + 1];
        const float duration = b.timestamp_seconds - a.timestamp_seconds;
        const float alpha = duration > 0.0f ? (t - a.timestamp_seconds) / duration : 0.0f;

        RailSampleData data;
        data.position = glm::mix(a.position.glm(), b.position.glm(), alpha);
        data.rotation = glm::slerp(a.rotation.glm(), b.rotation.glm(), alpha);
        data.color = glm::mix(a.color.glm(), b.color.glm(), alpha);
        callback->second(data);
    }

    if (all_done)
    {
        active = false;
        if (loop)
            start();
    }
}

namespace
{
    void tick_rails(ecs::Query<Rail> rails, ecs::Res<ecs::FrameTime> time)
    {
        rails.each([&] (Rail& rail) { rail.tick(time->dt); });
    }
}

void install_rail(World& world)
{
    scene::register_component_type<Rail>();
    world.schedule.add<&tick_rails>(ecs::Phase::Update);
}
