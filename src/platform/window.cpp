module platform:window;
import <cassert>;
import <glfw/glfw3.h>;


static void framebuffer_resize_callback(GLFWwindow* hwnd, int w, int h) {
    auto window = reinterpret_cast<platform::window::Window*>(glfwGetWindowUserPointer(hwnd));
    window->width = w;
    window->height = h;
    window->resized = true;
}

bool platform::window::window_create(Window& window, int width, int height, const char* title) {
    if (!glfwInit())
        return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window.handle = glfwCreateWindow(width, height, title, nullptr, nullptr);
    assert(window.handle);

    window.width = width;
    window.height = height;

    glfwSetWindowUserPointer(window.handle, &window);
    glfwSetFramebufferSizeCallback(window.handle, &framebuffer_resize_callback);

    return true;
}

void platform::window::window_poll_events() {
    glfwPollEvents();
}

bool platform::window::window_should_close(const Window& window) {
    return glfwWindowShouldClose(window.handle);
}

void platform::window::window_destroy(Window& window) {
    glfwDestroyWindow(window.handle);
    glfwTerminate();
}
