#include "platform/window.h"
#include "render/backends/vk/vk_render_backend.h"

int main() {
    using namespace platform::window;
    Window window;
    window_create(window, 1280, 720, "Rhea");
    
    auto render_backend = RenderBackend::create_backend<VkRenderBackend>();
    
    render_backend->init(window.handle);

    while (!window_should_close(window)) {
        window_poll_events();
        render_backend->draw_frame();
    }
    window_destroy(window);
    return 0;
}
