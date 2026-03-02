export module type_utils;

import <type_traits>;
import <variant>;

export template<typename... Ts>
struct TypeList
{
    template<typename T>
    static constexpr bool contains = (std::is_same_v<T, Ts> || ...);
};

export template<typename... Ts, typename F>
void visit_variant_types(const std::variant<Ts...>&, F&& func)
{
    (func.template operator()<Ts>(), ...);
}