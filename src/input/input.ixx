module;

#include <GLFW/glfw3.h>

export module input;

import std.compat;


export enum class Key
{
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z, _0, _1, _2, _3, _4, _5, _6, _7, _8, _9, 
    MouseLeft, MouseRight, MouseMiddle,
    Space, LeftShift,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};


export class Input {
public:
    void set_key(Key key, bool pressed) {
        keys[key] = pressed;
    }

    bool is_key_down(Key key) const {
        if (is_mouse_key(key) ? mouse_captured : keyboard_captured)
            return false;
        return is_key_physically_down(key);
    }

    // ignores capture by the UI
    bool is_key_physically_down(Key key) const {
        return keys.contains(key) && keys.at(key);
    }

    // mouse wheel, accumulated until consumed
    void add_scroll(double delta) {
        scroll_delta += delta;
    }
    
    double consume_scroll() {
        const double delta = mouse_captured ? 0.0 : scroll_delta;
        scroll_delta = 0.0;
        return delta;
    }

    // Set every frame by the debug UI: while it uses the mouse / keyboard (a hovered window,
    // an active text field) the game sees those keys as released.
    void set_ui_capture(bool mouse, bool keyboard) {
        mouse_captured = mouse;
        keyboard_captured = keyboard;
    }

    bool is_mouse_captured() const { return mouse_captured; }
    bool is_keyboard_captured() const { return keyboard_captured; }

    static constexpr bool is_mouse_key(Key key) {
        return key == Key::MouseLeft || key == Key::MouseRight || key == Key::MouseMiddle;
    }

    void set_mouse_pos(double x, double y) {
        mouse_x = x;
        mouse_y = y;
    }

    double mouse_x = 0.0;
    double mouse_y = 0.0;

private:
    std::unordered_map<Key, bool> keys;
    double scroll_delta = 0.0;
    bool mouse_captured = false;
    bool keyboard_captured = false;
};
