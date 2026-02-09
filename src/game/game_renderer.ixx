export module game:renderer;
import <memory>;
import render;
import engine;
import <map>;
import <vector>;
import name;
import glm;
import assets;
import texture_format;
import <optional>;
import <array>;
import <vector>;
import assets;
import <string>;

export class GameRenderer : public Renderer
{
public:    
    void set_engine(const std::shared_ptr<Engine>& in_engine);

    void init(RBWindowHandle in_window) override;

    std::shared_ptr<Engine> engine;
    
    void capture_ibl(glm::vec3 pos, Name actor_name);
    void finish_capturing_ibl(const std::string& filename);
    
    std::map<std::string, Cubemap> current_cubemaps;
};
