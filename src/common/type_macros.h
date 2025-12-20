#pragma once
#include <compare>

#define DEFAULT_NON_COPYABLE(Class) \
    Class(const Class&) = delete; \
    Class& operator=(const Class&) = delete; \
    Class(Class&&) = default; \
    Class& operator=(Class&&) = default; \
    Class() = default; \
    ~Class() = default;

#define AUTO_SPACESHIP(Class, Field) \
    friend auto operator<=>( const Class& lhs, const Class& rhs) \
    { \
        return lhs.Field <=> rhs.Field; \
    }