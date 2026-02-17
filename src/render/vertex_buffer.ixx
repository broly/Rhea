export module render:vertex_buffer;

import <cstdint>;

export struct VertexBufferDesc
{
    uint32_t frame_size;
    bool dynamic;
    
};