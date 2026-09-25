export module string_utils;

import std.compat;

export template<typename... Ts>
std::string format(const char* s, Ts&&... vs)
{
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), s, std::forward<Ts>(vs)...);
    return buffer;
}