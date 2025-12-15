#pragma once
#include <memory>

class RenderBackend 
{
public:
    virtual ~RenderBackend() = default;

    template<typename T>
    requires std::is_base_of_v<RenderBackend, T>
    static std::unique_ptr<RenderBackend> create_backend()
    {
        return std::unique_ptr<RenderBackend>(new T());
    }
    
    virtual void init(void* window) = 0;
    virtual void draw_frame() = 0;
};
