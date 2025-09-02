#pragma once
#include <vector>
#include <cstdint>
#include <optional>

struct ElfModule {
    uint64_t entry = 0;
    std::vector<uint8_t> code;
};

class ElfLoader {
public:
    // Very small fake ELF: [magic 4B] 'ELF1', [entry u64], [code_size u64], [code bytes]
    // TODO: Implement ELF loading from bytes, no fakeELF
    std::optional<ElfModule> load_from_bytes(const std::vector<uint8_t>& bytes);
};
