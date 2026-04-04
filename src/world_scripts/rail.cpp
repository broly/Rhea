module rail;

void Rail::tick(const double dt)
{
    RhActor::tick(dt);

    if (!active || samples.empty())
        return;

    accumulated_time += dt * time_dilation;

    float t = get_passed_time();

    bool all_done = true;

    for (auto& [name, track] : samples)
    {
        if (track.empty())
            continue;

        auto cb_it = on_tick_map.find(name);
        if (cb_it == on_tick_map.end())
            continue;

        auto& on_tick = cb_it->second;

        if (!on_tick)
            continue;

        if (t < track.back().timestamp_seconds)
            all_done = false;

        if (track.size() == 1)
        {
            RailSampleData data{
                track[0].position,
                track[0].rotation,
                track[0].color
            };
            on_tick(data);
            continue;
        }

        if (t >= track.back().timestamp_seconds)
        {
            const RailSample& last = track.back();

            RailSampleData data{
                last.position,
                last.rotation,
                last.color
            };

            on_tick(data);
            continue;
        }

        int segment = -1;
        for (int i = 0; i < (int)track.size() - 1; ++i)
        {
            if (t >= track[i].timestamp_seconds &&
                t < track[i + 1].timestamp_seconds)
            {
                segment = i;
                break;
            }
        }

        if (segment == -1)
            continue;

        const RailSample& a = track[segment];
        const RailSample& b = track[segment + 1];

        float duration = b.timestamp_seconds - a.timestamp_seconds;

        float alpha = duration > 0.0f
            ? (t - a.timestamp_seconds) / duration
            : 0.0f;

        RailSampleData data;
        data.position = glm::mix(a.position.glm(), b.position.glm(), alpha);
        data.rotation = glm::slerp(a.rotation.glm(), b.rotation.glm(), alpha);
        data.color    = glm::mix(a.color.glm(), b.color.glm(), alpha);

        on_tick(data);
    }

    if (all_done)
    {
        active = false;
    }
}
void Rail::startup()
{
    start_time = world->get_time_seconds();
    active = true;
}

void Rail::on_serialize(const SerializationContext& context)
{
    RhActor::on_serialize(context);
}

float Rail::get_passed_time() const
{
    return accumulated_time;
}
