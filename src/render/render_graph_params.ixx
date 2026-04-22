export module render:rg_params;

import <map>;
import name;
import glm;

export struct RenderGraphParameters
{
    uint32_t render_id = 0;
    uint32_t num_runs = 0;
    uint32_t frame_id = 0;
    std::map<Name, int> int_params;
    std::map<Name, bool> bool_params;
    std::map<Name, glm::vec3> vec3_params;
};
