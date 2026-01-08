export module platform:window;

import <GLFW/glfw3.h>;

import input;

export namespace platform
{
    namespace window
    {
        struct Window {
            GLFWwindow* handle = nullptr;
            int width = 0;
            int height = 0;
            bool resized = false;
        };
        
        void set_input(Input* input);
        bool window_create(Window& window, int width, int height, const char* title);
        void window_poll_events();
        bool window_should_close(const Window& window);
        void window_destroy(Window& window);
    }
}

namespace plat = platform;
