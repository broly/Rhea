#pragma once

import fixed_string;

#define DEFINE_VERBOSITY(name, level, details, printer) \
    export inline constexpr auto name = LogVerbosity(level, static_cast<ELogOutputDetailsMask>(details), &printer); \
    export template<> \
    struct LogVerbosityLevelTraits<level> \
    { \
        static constexpr inline LogVerbosity verbosity = name; \
    }



#define LOGGER_CONCAT_TWO(a, b) a##b
#define LOGGER_FIXED_STRING(s) LOGGER_CONCAT_TWO(#s,_fixed)

#define DEFINE_LOGGER(LoggerName, DefaultVerbosity) \
    constexpr Logger<DefaultVerbosity, LOGGER_FIXED_STRING(LoggerName)> LoggerName{}