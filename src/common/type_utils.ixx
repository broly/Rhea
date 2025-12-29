export module type_utils;

import <type_traits>;

export template<typename... Ts>
struct TypeList
{
    template<typename T>
    static constexpr bool contains = (std::is_same_v<T, Ts> || ...);
};
