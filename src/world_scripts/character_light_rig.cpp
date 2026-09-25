module character_light_rig;

import std.compat;
import fixed_string;

import log;

#include "logging/log_macro.h"

DEFINE_LOGGER(LogLightRig, Log);


using RigLight = CharacterLightRig::RigLight;

static glm::vec3 rgb(float r, float g, float b, float intensity)
{
    return glm::vec3(r, g, b) * intensity;
}

// offsets: { forward, right, up }
static const CharacterLightRig::Preset PRESETS[] =
{
    {
        "golden hour",
        {{
            { { 1.6f,  1.1f, 2.0f}, rgb(1.00f, 0.62f, 0.32f, 16.0f) },  // key: amber, front side high
            { {-1.4f, -0.9f, 2.3f}, rgb(0.25f, 0.70f, 1.00f, 14.0f) },  // rim: teal, back
            { { 1.8f, -1.4f, 1.1f}, rgb(0.55f, 0.50f, 0.90f,  4.0f) },  // fill: lavender
            { { 0.9f,  0.0f, 0.3f}, rgb(1.00f, 0.70f, 0.45f,  2.0f) },  // floor bounce
            { {-0.8f,  1.3f, 1.4f}, rgb(1.00f, 0.75f, 0.40f,  5.0f) },  // kicker: gold, back side
            { { 0.2f,  0.0f, 2.6f}, rgb(1.00f, 0.85f, 0.70f,  3.0f) },  // top: hair sheen
        }},
        glm::vec3(1.0f, 0.75f, 0.5f) * 0.6f
    },
    {
        "neon night",
        {{
            { { 0.6f,  1.6f, 1.6f}, rgb(1.00f, 0.15f, 0.55f, 18.0f) },  // key: magenta, side
            { {-1.2f, -1.2f, 2.0f}, rgb(0.10f, 0.85f, 1.00f, 18.0f) },  // rim: cyan, opposite side
            { { 1.8f,  0.0f, 1.2f}, rgb(0.20f, 0.30f, 1.00f,  5.0f) },  // fill: deep blue, front
            { { 0.8f,  0.0f, 0.2f}, rgb(0.50f, 0.15f, 0.90f,  3.0f) },  // floor bounce: purple
            { {-1.0f,  1.0f, 1.0f}, rgb(1.00f, 0.30f, 0.80f,  8.0f) },  // kicker: pink
            { { 0.0f,  0.0f, 2.6f}, rgb(0.30f, 0.80f, 1.00f,  3.0f) },  // top: cyan
        }},
        glm::vec3(0.15f, 0.15f, 0.3f)
    },
    {
        "moonlight",
        {{
            { { 1.2f, -1.0f, 2.4f}, rgb(0.55f, 0.70f, 1.00f, 12.0f) },  // key: cold moon
            { {-1.0f,  1.2f, 0.9f}, rgb(1.00f, 0.50f, 0.20f,  9.0f) },  // rim: warm candle, low
            { { 1.8f,  1.2f, 1.2f}, rgb(0.40f, 0.45f, 0.70f,  3.0f) },  // fill
            { { 0.8f,  0.0f, 0.2f}, rgb(0.30f, 0.35f, 0.60f,  1.5f) },  // floor bounce
            { {-1.3f, -0.6f, 2.2f}, rgb(0.60f, 0.80f, 1.00f,  8.0f) },  // kicker: cold edge
            { { 0.0f,  0.0f, 2.5f}, rgb(0.50f, 0.60f, 1.00f,  2.0f) },  // top
        }},
        glm::vec3(0.2f, 0.3f, 0.6f) * 0.25f
    },
    {
        "sunset drama",
        {{
            { {-1.5f,  0.4f, 2.0f}, rgb(1.00f, 0.40f, 0.12f, 24.0f) },  // strong orange back light
            { { 0.4f,  1.7f, 1.2f}, rgb(1.00f, 0.30f, 0.15f, 10.0f) },  // key: red orange, side low
            { { 1.8f, -0.6f, 1.3f}, rgb(0.45f, 0.25f, 0.80f,  6.0f) },  // fill: purple
            { { 0.8f,  0.0f, 0.2f}, rgb(0.80f, 0.35f, 0.30f,  2.0f) },  // floor bounce
            { {-0.6f, -1.4f, 1.6f}, rgb(0.90f, 0.25f, 0.50f,  7.0f) },  // kicker: rose
            { { 0.0f,  0.0f, 2.6f}, rgb(1.00f, 0.60f, 0.40f,  2.0f) },  // top
        }},
        glm::vec3(1.0f, 0.45f, 0.25f) * 0.5f
    },
    {
        "studio soft",
        {{
            { { 1.5f,  0.9f, 1.9f}, rgb(1.00f, 0.88f, 0.75f, 12.0f) },  // key: warm white
            { { 1.7f, -1.3f, 1.2f}, rgb(0.70f, 0.80f, 1.00f,  6.0f) },  // fill: cool
            { {-1.2f,  0.3f, 2.3f}, rgb(1.00f, 0.95f, 0.90f, 10.0f) },  // rim
            { { 0.9f,  0.0f, 0.3f}, rgb(0.90f, 0.75f, 0.60f,  2.5f) },  // floor bounce
            { {-1.0f, -1.0f, 1.2f}, rgb(0.90f, 0.90f, 1.00f,  3.0f) },  // kicker
            { { 0.0f,  0.0f, 2.6f}, rgb(1.00f, 1.00f, 1.00f,  2.0f) },  // top
        }},
        glm::vec3(1.0f)
    },
};

static constexpr size_t NUM_PRESETS = sizeof(PRESETS) / sizeof(PRESETS[0]);


bool CharacterLightRig::init(World& world, std::shared_ptr<RhActor> in_character, const std::string& light_actor_prefix)
{
    character = in_character;
    if (!character)
        return false;

    for (size_t i = 0; i < NUM_LIGHTS; ++i)
    {
        auto actor = world.find_actor_by_name(light_actor_prefix + std::to_string(i));
        if (!actor || !actor->find_component<RhComp_Light>())
        {
            LogLightRig.Log("Light rig: no light actor '%s%zu'", light_actor_prefix.c_str(), i);
            return false;
        }
        light_actors.push_back(actor);
    }

    apply_preset_log();
    return true;
}

void CharacterLightRig::next_preset()
{
    preset_index = (preset_index + 1) % NUM_PRESETS;
    apply_preset_log();
}

void CharacterLightRig::previous_preset()
{
    preset_index = (preset_index + NUM_PRESETS - 1) % NUM_PRESETS;
    apply_preset_log();
}

const char* CharacterLightRig::get_preset_name() const
{
    return PRESETS[preset_index].name;
}

size_t CharacterLightRig::get_preset_count() const
{
    return NUM_PRESETS;
}

const char* CharacterLightRig::get_preset_name(size_t index) const
{
    return PRESETS[index].name;
}

void CharacterLightRig::set_preset(size_t index)
{
    preset_index = index % NUM_PRESETS;
    apply_preset_log();
}

glm::vec3 CharacterLightRig::get_sun_tint() const
{
    return PRESETS[preset_index].sun_tint;
}

void CharacterLightRig::apply_preset_log() const
{
    LogLightRig.Log("Lighting preset %zu/%zu: %s", preset_index + 1, NUM_PRESETS, get_preset_name());
}

void CharacterLightRig::tick()
{
    const Transform ct = character->get_transform();
    const glm::vec3 base = ct.position.glm();
    const glm::vec3 forward = ct.rotation.glm() * glm::vec3(0, 0, 1);   // glTF characters face +Z
    const glm::vec3 right = ct.rotation.glm() * glm::vec3(-1, 0, 0);
    const glm::vec3 up = glm::vec3(0, 1, 0);

    const Preset& preset = PRESETS[preset_index];

    for (size_t i = 0; i < NUM_LIGHTS; ++i)
    {
        const RigLight& rig_light = preset.lights[i];
        auto light = light_actors[i]->find_component<RhComp_Light>();

        light->color = glm::vec4(rig_light.color, 0.0f);
        light->update_scene_proxy();

        Transform t = light_actors[i]->get_transform();
        t.position = base + forward * rig_light.offset.x + right * rig_light.offset.y + up * rig_light.offset.z;
        light_actors[i]->set_transform(t);   // submits the proxy (color included)
    }
}
