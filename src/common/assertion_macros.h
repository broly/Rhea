#pragma once
#include <source_location>
#include <iostream>

inline bool ensure_impl(bool value, std::source_location sl = std::source_location::current())
{
    if (!value)
    {
        std::cerr << "ensure condition failed: " << sl.file_name() << ":" << sl.line() << " at " << sl.function_name() << std::endl;
        __debugbreak();
    }
    return value;
}


void print_error_no_text(std::source_location sl)
{
    std::cerr << "ensure condition failed: " << sl.file_name() << ":" << sl.line() << " at " << sl.function_name() << std::endl;
}


#define ENSURE_IMPL(v) \
    [](decltype(v) value, std::source_location sl = std::source_location::current()) -> decltype(v) \
    { \
        if (!value) \
        { \
            print_error_no_text(sl); \
            __debugbreak(); \
        }\
        return value; \
    }(v)

#define ensure(v) (ENSURE_IMPL(v))