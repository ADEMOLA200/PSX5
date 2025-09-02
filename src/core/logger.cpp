#include "logger.h"
#include <mutex>

namespace {
std::mutex mtx;
log::Level g_level = log::Level::Info;

constexpr const char* tag(log::Level l) {
    switch(l){
        case log::Level::Trace: return "[TRACE] ";
        case log::Level::Debug: return "[DEBUG] ";
        case log::Level::Info:  return "[INFO ] ";
        case log::Level::Warn:  return "[WARN ] ";
        case log::Level::Error: return "[ERROR] ";
        case log::Level::Fatal: return "[FATAL] ";
    }
    return "";
}

bool enabled(log::Level l){
    return static_cast<int>(l) >= static_cast<int>(g_level);
}

void out(log::Level l, std::string_view s){
    if(!enabled(l)) return;
    std::lock_guard<std::mutex> lock(mtx);
    std::fwrite(tag(l), 1, std::char_traits<char>::length(tag(l)), stdout);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fwrite("\n", 1, 1, stdout);
    if(l == log::Level::Fatal) std::fflush(stdout);
}
}

namespace log {
void set_level(Level lvl){ g_level = lvl; }
void trace(std::string_view s){ out(Level::Trace, s); }
void debug(std::string_view s){ out(Level::Debug, s); }
void info (std::string_view s){ out(Level::Info , s); }
void warn (std::string_view s){ out(Level::Warn , s); }
void error(std::string_view s){ out(Level::Error, s); }
void fatal(std::string_view s){ out(Level::Fatal, s); }
}
