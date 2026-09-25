export module character_light_rig;

import std.compat;

import glm;
import rhmath;
import framework;
import rhcomponents;

// Point lights which follow a character and light it with artistic presets.
// Light offsets are in the character frame (forward / right / up, meters), so the
// lighting stays the same wherever the character stands or looks.
// Point lights: radiance = color / (distance^2 + 1), no shadows.
export class CharacterLightRig
{
public:
    static constexpr size_t NUM_LIGHTS = 6;

    struct RigLight
    {
        glm::vec3 offset;   // forward, right, up
        glm::vec3 color;    // linear rgb * intensity
    };

    struct Preset
    {
        const char* name;
        std::array<RigLight, NUM_LIGHTS> lights;
        glm::vec3 sun_tint;  // multiplies the (animated) sun color
    };

    // light actors are named <prefix>0 .. <prefix>5, each with a RhComp_Light
    bool init(World& world, std::shared_ptr<RhActor> in_character, const std::string& light_actor_prefix);

    void next_preset();
    void previous_preset();
    const char* get_preset_name() const;
    
    size_t get_preset_count() const;
    size_t get_preset_index() const { return preset_index; }
    const char* get_preset_name(size_t index) const;
    void set_preset(size_t index);
    glm::vec3 get_sun_tint() const;

    void tick();

private:
    void apply_preset_log() const;

    std::shared_ptr<RhActor> character;
    std::vector<std::shared_ptr<RhActor>> light_actors;
    size_t preset_index = 0;
};
