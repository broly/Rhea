#pragma once

#define DEFINE_VERBOSITY(name, level, details, printer) \
    export inline constexpr auto name = LogVerbosity(level, static_cast<ELogOutputDetailsMask>(details), &printer); \
    export template<> \
    struct LogVerbosityLevelTraits<level> \
    { \
        static constexpr inline LogVerbosity verbosity = name; \
    }


#define DEFINE_LOGGER(LoggerName, DefaultVerbosity) \
    constexpr Logger LoggerName(#LoggerName, DefaultVerbosity)