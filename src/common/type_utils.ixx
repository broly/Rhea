export module type_utils;

import <type_traits>;
import <variant>;

export template<typename... Ts>
struct TypeList
{
    template<typename T>
    static constexpr bool contains = (std::is_same_v<T, Ts> || ...);
};

export template<template<typename...> typename T, typename... Ts, typename F>
constexpr void visit_variadic_types(const T<Ts...>&, F&& func)
{
    (func.template operator()<Ts>(), ...);
}