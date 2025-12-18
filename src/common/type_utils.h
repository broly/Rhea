#pragma once
#include <type_traits>

template<typename... Ts>
struct TypeList
{
    template<typename T>
    static constexpr bool contains = (std::is_same_v<T, Ts> || ...);
};
