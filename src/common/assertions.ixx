export module assertions;

import std.compat;


#include "fmt_helpers.h"

// printf-style varargs only accept trivially copyable types. Handle wrappers
// (anything with a `handle` member, e.g. RBHandle) are passed as the raw handle,
// so "%p" prints the handle value.
export template<typename T>
decltype(auto) printf_arg(T&& value)
{
    if constexpr (requires { value.handle; } && std::is_class_v<std::remove_cvref_t<T>>)
        return (void*)(uintptr_t)value.handle;
    else
        return std::forward<T>(value);
}

export inline bool ensure_impl(bool value, std::source_location sl = std::source_location::current())
{
    if (!value)
    {
        std::cerr << "ensure condition failed: " << sl.file_name() << ":" << sl.line() << " at " << sl.function_name() << std::endl;
        __builtin_debugtrap();
    }
    return value;
}


export inline void print_error_no_text(std::source_location sl)
{
    std::cerr << "assertion failed: " << sl.file_name() << ":" << sl.line() << " at " << sl.function_name() << std::endl;
}

export template<typename Fmt, typename... Args>
HIGHLIGHT_FORMAT inline void print_error(std::source_location sl, Fmt&& fmt, Args&&... args)
{
    std::cerr << "assertion failed: " << sl.file_name() << ":" << sl.line() << " at " << sl.function_name() << std::endl;
    
    constexpr size_t buf_count = 1000;
    
    char buffer[buf_count] = "\0";
    std::snprintf(buffer, buf_count, fmt, printf_arg(std::forward<Args>(args))...);
    
    std::cerr << buffer << std::endl;
}

