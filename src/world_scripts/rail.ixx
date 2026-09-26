module;

#include <json/value.h>

export module rail;

import std.compat;
import reflect;
import framework;
import glm;
import name;
import rhmath;
import rhobject;

export struct RailSampleData
{
    vec3 position;
    quat rotation;
    vec4 color;
};


export struct RailSample : RailSampleData
{
    float timestamp_seconds;
};


export using RailCallback = std::function<void(const RailSampleData& data)>;

// Named tracks of timed samples (position, rotation, color), played back after start():
// every frame the callback of each track gets the interpolated sample.
//
//     "Rail": { "time_dilation": 1, "samples": { "light": [ { "timestamp_seconds": 0, "position": {...}, ... } ] } }
export struct Rail
{
    std::map<Name, std::vector<RailSample>> samples;

    [[=rh::edit, =rh::range<0.f, 5.f>]]
    float time_dilation = 1.0f;

    // advance by `timestep` per frame instead of the frame time
    [[=rh::edit]]
    bool fixed_timestep = false;
    std::optional<float> timestep = std::nullopt;

    [[=rh::edit]]
    bool loop = false;

    [[=rh::edit, =rh::read_only, =rh::transient]]
    bool active = false;

    [[=rh::edit, =rh::read_only, =rh::transient]]
    float accumulated_time = 0.0f;

    [[=rh::transient]]
    std::map<Name, RailCallback> on_tick;

    // (Re)starts the playback from the first sample
    void start();
    void tick(double dt);
};

// Component type and the playback system (Update phase)
export void install_rail(World& world);
