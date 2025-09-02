#pragma once
#include <vector>
#include <cstdint>
#include <optional>

struct ElfModule {
    uint64_t entry = 0;
    std::vector<uint8_t> code;
    uint64_t load_address = 0;
};

class Elf64Loader {
public:
    // Parses a minimal ELF64 and maps PT_LOAD segments into the target Memory at given base.
    // Returns ElfModule with entry and combined code blob and expected virtual base.
    std::optional<ElfModule> load_from_bytes(const std::vector<uint8_t>& bytes);
};
