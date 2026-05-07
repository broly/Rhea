export module string_utils;
import <string>;

export template<typename... Ts>
std::string format(const char* s, Ts&&... vs)
{
    char buffer[512];
    sprintf_s(buffer, sizeof(buffer), s, std::forward<Ts>(vs)...);
    return buffer;
}