export module assertions;

import <source_location>;
import <iostream>;

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
    std::cerr << "ensure condition failed: " << sl.file_name() << ":" << sl.line() << " at " << sl.function_name() << std::endl;
}

