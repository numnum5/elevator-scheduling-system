#pragma once

#include <iosfwd>
#include <source_location>
#include <string_view>

namespace logger {

void set_output(std::ostream& stream);

void write(
    std::string_view level,
    std::string_view message,
    const std::source_location& location
);

void info(
    std::string_view message,
    std::source_location location = std::source_location::current()
);

void debug(
    std::string_view message,
    std::source_location location = std::source_location::current()
);

void warn(
    std::string_view message,
    std::source_location location = std::source_location::current()
);

} // namespace logger
