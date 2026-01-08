module platform:window;
import <cassert>;
import <glfw/glfw3.h>;
import input;
import <map>;

static std::map<int, Key> GLFW_KEYS = {
    {GLFW_KEY_A, Key::A}, 
    {GLFW_KEY_B, Key::B}, 
    {GLFW_KEY_C, Key::C}, 
    {GLFW_KEY_D, Key::D}, 
    {GLFW_KEY_E, Key::E}, 
    {GLFW_KEY_F, Key::F}, 
    {GLFW_KEY_G, Key::G}, 
    {GLFW_KEY_H, Key::H}, 
    {GLFW_KEY_I, Key::I}, 
    {GLFW_KEY_J, Key::J}, 
    {GLFW_KEY_K, Key::K}, 
    {GLFW_KEY_L, Key::L}, 
    {GLFW_KEY_M, Key::M}, 
    {GLFW_KEY_N, Key::N}, 
    {GLFW_KEY_O, Key::O}, 
    {GLFW_KEY_P, Key::P}, 
    {GLFW_KEY_Q, Key::Q}, 
    {GLFW_KEY_R, Key::R}, 
    {GLFW_KEY_S, Key::S}, 
    {GLFW_KEY_T, Key::T}, 
    {GLFW_KEY_U, Key::U}, 
    {GLFW_KEY_V, Key::V}, 
    {GLFW_KEY_W, Key::W}, 
    {GLFW_KEY_X, Key::X}, 
    {GLFW_KEY_Y, Key::Y}, 
    {GLFW_KEY_Z, Key::Z},
};

static std::map<int, Key> GLFW_MOUSE_BUTTONS = {
    {GLFW_MOUSE_BUTTON_LEFT, Key::MouseLeft},
    {GLFW_MOUSE_BUTTON_RIGHT, Key::MouseRight},
    {GLFW_MOUSE_BUTTON_MIDDLE, Key::MouseMiddle},
};


static void framebuffer_resize_callback(GLFWwindow* hwnd, int w, int h) {
    auto window = reinterpret_cast<platform::window::Window*>(glfwGetWindowUserPointer(hwnd));
    window->width = w;
    window->height = h;
    window->resized = true;
}

static Input* g_input = nullptr;

void set_input(Input* input) {
    g_input = input;
}

static void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods) {
    if (!g_input) 
        return;
    g_input->set_key(GLFW_KEYS[key], action != GLFW_RELEASE);
}

static void mouse_callback(GLFWwindow* w, double x, double y) {
    if (!g_input) 
        return;
    g_input->set_mouse_pos(x, y);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (!g_input) 
        return;
    g_input->set_key(GLFW_MOUSE_BUTTONS[button], action != GLFW_RELEASE);
}


void platform::window::set_input(Input* input)
{
    g_input = input;
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
    glfwSetKeyCallback(window.handle, key_callback);
    glfwSetMouseButtonCallback(window.handle, mouse_button_callback);
    glfwSetCursorPosCallback(window.handle, mouse_callback);

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
