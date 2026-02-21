#pragma once
import <compare>;

#define DEFAULT_NON_COPYABLE(Class) \
    Class(const Class&) = delete; \
    Class& operator=(const Class&) = delete; \
    Class(Class&&) noexcept = default; \
    Class& operator=(Class&&) noexcept = default; \
    Class() = default; \
    ~Class() = default;

#define NON_COPYABLE(Class) \
    Class(const Class&) = delete; \
    Class& operator=(const Class&) = delete; \
    Class(Class&&) = default; \
    Class& operator=(Class&&) = default; 

#define AUTO_SPACESHIP(Class, Field) \
    friend auto operator<=>( const Class& lhs, const Class& rhs) \
    { \
        return lhs.Field <=> rhs.Field; \
    }

#define ENUM_MASK_OPS(Enum, _export) \
    _export inline Enum operator|(Enum a, Enum b) \
    { \
        return static_cast<Enum>( \
            static_cast<std::underlying_type_t<Enum>>(a) | static_cast<std::underlying_type_t<Enum>>(b) \
        ); \
    } \
    _export inline Enum operator|=(Enum& a, Enum b) \
    { \
        a = a | b; \
        return a; \
    } \
    _export inline Enum operator&(Enum a, Enum b) \
    { \
        return static_cast<Enum>( \
            static_cast<std::underlying_type_t<Enum>>(a) & static_cast<std::underlying_type_t<Enum>>(b) \
        ); \
    } \
    _export inline Enum operator~(Enum a) \
    { \
        return static_cast<Enum>( \
            ~static_cast<std::underlying_type_t<Enum>>(a) \
        ); \
    } 

#define ENUM_WRAPPER_STRUCT(s, e, f) \
    s(e v) \
        : f(v) \
    {}\
    s& operator=(e in_v) \
    { \
        f = in_v; \
        return *this;\
    }

    // operator e() const \
    // { \
    //     return f; \
    // } \