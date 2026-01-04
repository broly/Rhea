#pragma once
import <compare>;

#define DEFAULT_NON_COPYABLE(Class) \
    Class(const Class&) = delete; \
    Class& operator=(const Class&) = delete; \
    Class(Class&&) = default; \
    Class& operator=(Class&&) = default; \
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

#define ENUM_MASK_OPS(Enum) \
    inline Enum operator|(Enum a, Enum b) \
    { \
        return static_cast<Enum>( \
            static_cast<std::underlying_type_t<Enum>>(a) | static_cast<std::underlying_type_t<Enum>>(b) \
        ); \
    } \
    inline Enum operator|=(Enum& a, Enum b) \
    { \
        a = a | b; \
        return a; \
    } \
    inline Enum operator&(Enum a, Enum b) \
    { \
        return static_cast<Enum>( \
            static_cast<std::underlying_type_t<Enum>>(a) & static_cast<std::underlying_type_t<Enum>>(b) \
        ); \
    } \
    inline Enum operator~(Enum a) \
    { \
        return static_cast<Enum>( \
            ~static_cast<std::underlying_type_t<Enum>>(a) \
        ); \
    } 