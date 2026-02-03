export module game:renderer;
import <memory>;
import render;
import engine;
import <map>;
import <vector>;
import name;
import glm;
import assets;

export class GameRenderer : public Renderer
{
public:    
    void set_engine(const std::shared_ptr<Engine>& in_engine);

    void init(RBWindowHandle in_window) override;

    std::shared_ptr<Engine> engine;
    
    
    
};
