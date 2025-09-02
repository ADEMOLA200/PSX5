#include "elf_loader.h"
#include <cstring>
#include <iostream>

static const char MAGIC[4] = {'E','L','F','1'};

std::optional<ElfModule> ElfLoader::load_from_bytes(const std::vector<uint8_t>& bytes){
    if(bytes.size() < 4 + 8 + 8) return std::nullopt;
    if(std::memcmp(bytes.data(), MAGIC, 4) != 0) return std::nullopt;
    size_t off = 4;
    uint64_t entry = 0;
    std::memcpy(&entry, bytes.data()+off, 8); off += 8;
    uint64_t code_size = 0; std::memcpy(&code_size, bytes.data()+off, 8); off += 8;
    if(off + code_size > bytes.size()) return std::nullopt;
    ElfModule m;
    m.entry = entry;
    m.code.assign(bytes.begin() + off, bytes.begin() + off + code_size);
    return m;
}
