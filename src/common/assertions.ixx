export module assertions;

import <source_location>;
import <iostream>;

#include "fmt_helpers.h"

export inline bool ensure_impl(bool value, std::source_location sl = std::source_location::current())
{
    if (!value)
    {
        std::cerr << "ensure condition failed: " << sl.file_name() << ":" << sl.line() << " at " << sl.function_name() << std::endl;
        __debugbreak();
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
    sprintf_s(buffer, buf_count, fmt, std::forward<Args>(args)...);
    
    std::cerr << buffer << std::endl;
}

