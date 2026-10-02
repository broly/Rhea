module;

#include <GLFW/glfw3.h>

export module platform:window;


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
        
        struct WindowCreateOptions
        {
            bool maximized = false;
            // false: the window opens without taking the keyboard focus (unattended runs)
            bool focus_on_show = true;
        };

        void set_input(Input* input);
        bool window_create(Window& window, int width, int height, const char* title, const WindowCreateOptions& options = {});
        void window_request_close(Window& window);
        void window_poll_events();
        // hidden cursor locked to the window, unlimited mouse movement (mouse look); raw motion where supported
        void set_cursor_captured(Window& window, bool captured);
        bool window_should_close(const Window& window);
        void window_destroy(Window& window);
    }
}

namespace plat = platform;
