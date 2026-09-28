export module game:renderer;

import std.compat;
import render;
import engine;
import name;
import glm;
import assets;
import texture_format;
import assets;
import :reflection_probes;

export class GameRenderer : public Renderer
{
public:    
    void set_engine(const std::shared_ptr<Engine>& in_engine);

    void init(RBWindowHandle in_window) override;

    std::shared_ptr<Engine> engine;
    
    // runtime reflection probes of the main render graph (null if it has none)
    ReflectionProbeSystem* get_reflection_probes() const;
    
    // every reflection probe is rebaked (a probe per few frames, nearest first)
    void rebake_reflection_probes();
};
