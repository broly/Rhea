#pragma once

#define DEFAULT_NON_COPYABLE(Class) \
    Class(const Class&) = delete; \
    Class& operator=(const Class&) = delete; \
    Class(Class&&) = default; \
    Class& operator=(Class&&) = default; \
    Class() = default; \
    ~Class() = default;