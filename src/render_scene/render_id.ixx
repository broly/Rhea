export module render_scene:render_id;

import <cstdint>;

export struct RenderId
{
    uint64_t identifier;
    uint32_t generation;
};

export using SceneViewProcId = unsigned;