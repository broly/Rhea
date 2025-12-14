#include "platform/window.h"

int main() {
    using namespace platform::window;
    Window window;
    window_create(window, 1280, 720, "Rhea");

    while (!window_should_close(window)) {
        window_poll_events();
    }
    window_destroy(window);
    return 0;
}
