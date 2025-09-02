#pragma once

#include "../core/types.h"
#include <vector>
#include <string>

namespace PS5Emu {

class SecureLoader {
public:
    struct ExecutableHeader {
        uint32_t magic;
        uint32_t version;
        uint64_t entry_point;
        uint64_t code_size;
        uint64_t data_size;
        uint32_t flags;
        uint8_t signature[256];
    };
    
    struct LoadedModule {
        std::string name;
        uint64_t base_address;
        uint64_t size;
        uint64_t entry_point;
        bool is_system_module;
        std::vector<uint8_t> code;
    };

private:
    std::vector<LoadedModule> loaded_modules;
    bool secure_mode_enabled;
    
public:
    SecureLoader();
    
    bool LoadExecutable(const std::string& path, uint64_t load_address);
    bool LoadSystemModule(const std::string& name, const void* data, size_t size);
    bool VerifySignature(const ExecutableHeader& header, const void* data, size_t size);
    
    const std::vector<LoadedModule>& GetLoadedModules() const { return loaded_modules; }
    void SetSecureMode(bool enabled) { secure_mode_enabled = enabled; }
    bool IsSecureModeEnabled() const { return secure_mode_enabled; }
};

} // namespace PS5Emu
