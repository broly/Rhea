export module input;

import <unordered_map>;
import <GLFW/glfw3.h>;

export enum class Key
{
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    MouseLeft, MouseRight, MouseMiddle,
};


export class Input {
public:
    void set_key(Key key, bool pressed) {
        keys[key] = pressed;
    }

    bool is_key_down(Key key) const {
        return keys.contains(key) && keys.at(key);
    }

    void set_mouse_pos(double x, double y) {
        mouse_x = x;
        mouse_y = y;
    }

    double mouse_x = 0.0;
    double mouse_y = 0.0;

private:
    std::unordered_map<Key, bool> keys;
};
