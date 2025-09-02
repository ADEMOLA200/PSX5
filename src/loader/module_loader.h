#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <optional>

struct Module {
    std::vector<uint8_t> code;
    uint64_t entry = 0;
    std::string name;        // Added module name for PKG support
    std::string version;     // Added version info for PKG support
    std::string content_id;  // Added content ID for PKG support
};

class ModuleLoader {
public:
    std::optional<Module> from_bytes(const std::vector<uint8_t>& bytes);
    std::optional<Module> from_file(const std::string& filepath);  // Added file loading support
    bool is_pkg_file(const std::vector<uint8_t>& bytes);          // Added PKG detection
};
