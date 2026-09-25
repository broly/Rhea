export module render:vertex_buffer;

import std.compat;


export struct VertexBufferDesc
{
    uint32_t frame_size;
    bool dynamic;
    
};