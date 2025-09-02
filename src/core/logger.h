#pragma once
#include <cstdio>
#include <string_view>

namespace log {
enum class Level { Trace, Debug, Info, Warn, Error, Fatal };

void set_level(Level lvl);
void trace(std::string_view s);
void debug(std::string_view s);
void info(std::string_view s);
void warn(std::string_view s);
void error(std::string_view s);
void fatal(std::string_view s);
}
