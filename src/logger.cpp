#include "logger.h"

#include <iostream>
#include <mutex>

namespace logger {
    namespace {
        std::mutex output_mutex;
        std::ostream* output = &std::clog;
    }

    void set_output(std::ostream& stream) {
        std::lock_guard lock(output_mutex);
        output = &stream;
    }

    void write(
        std::string_view level,
        std::string_view message,
        const std::source_location& location
    ) {
        std::lock_guard lock(output_mutex);

        *output << '[' << level << "] "
                << location.file_name() << ':' << location.line()
                << " [" << location.function_name() << "] "
                << message << '\n';
    }

    void info(
        std::string_view message,
        std::source_location location
    ) {
        write("INFO", message, location);
    }

    void debug(
        std::string_view message,
        std::source_location location
    ) {
        write("DEBUG", message, location);
    }

    void warn(
        std::string_view message,
        std::source_location location
    ) {
        write("WARN", message, location);
    }

} // namespace logger
