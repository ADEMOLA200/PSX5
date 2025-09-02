#include "module_loader.h"
#include "elf64_loader.h"
#include "pkg_loader.h"  // Added PKG loader support
#include <iostream>
#include <fstream>

bool ModuleLoader::is_pkg_file(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 4) return false;
    uint32_t magic = *reinterpret_cast<const uint32_t*>(bytes.data());
    return magic == 0x7F504B47; // ".PKG" magic
}

std::optional<Module> ModuleLoader::from_bytes(const std::vector<uint8_t>& bytes){
    if (is_pkg_file(bytes)) {
        PS5Emu::PKGLoader pkg_loader;
        auto pkg = pkg_loader.load_from_bytes(bytes);
        if (pkg) {
            std::vector<uint8_t> executable;
            if (pkg_loader.extract_executable(*pkg, executable)) {
                Module m;
                m.code = executable;
                m.entry = 0; // PKG executables start at 0
                m.name = pkg->title_id;
                m.version = pkg->version;
                m.content_id = pkg->content_id;
                return m;
            }
        }
    }
    
    // Try ELF64 format
    Elf64Loader elf;
    auto em = elf.load_from_bytes(bytes);
    if(!em) {
        // Fallback: raw blob
        if(bytes.empty()) return std::nullopt;
        Module m; 
        m.code = bytes; 
        m.entry = 0;
        m.name = "raw_binary";
        return m;
    }
    Module m;
    m.code = em->code;
    m.entry = em->entry;
    m.name = "elf_binary";
    return m;
}

std::optional<Module> ModuleLoader::from_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    
    size_t size = file.tellg();
    file.seekg(0);
    
    std::vector<uint8_t> bytes(size);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    
    return from_bytes(bytes);
}
