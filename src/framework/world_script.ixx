export module framework:world_script;

import std.compat;
import properties;


import :core;

export class WorldScript
{
public:
    virtual ~WorldScript() = default;

    void init(std::shared_ptr<World> in_world)
    {
        world = in_world;
        startup();
    }


    virtual void startup() {}
    virtual void tick(double dt) {}

    // Debug UI ("World Scripts" window): title, [[=rh::edit]] fields
    // (return reflect::make_property_object(*this)) and custom widgets (ImGui calls).
    virtual std::string_view get_debug_name() const { return "WorldScript"; }
    virtual reflect::PropertyObject get_properties() { return {}; }
    virtual void draw_debug_ui() {}

    std::shared_ptr<World> world;
};
