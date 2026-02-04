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

struct CapturingCubemap
{
    TextureFormat ibl_format;
    std::array<std::vector<float>, 6> faces = {};
};

export class GameRenderer : public Renderer
{
public:    
    void set_engine(const std::shared_ptr<Engine>& in_engine);

    void init(RBWindowHandle in_window) override;

    std::shared_ptr<Engine> engine;
    
    void capture_ibl(glm::vec3 pos, Name actor_name);
    void finish_capturing_ibl(Name actor_name);
    
    std::map<Name, CapturingCubemap> current_cubemaps;
};
