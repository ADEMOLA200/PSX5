#pragma once
#include <functional>
#include <optional>
#include <cstddef>

class Memory;
class CPU;

struct JitCompiled {
    std::function<void(CPU&)> invoke;
    size_t bytes_consumed;
};

class JITAsm {
public:
    // Attempts to compile a short sequence starting at pc in mem.
    // Returns nullopt if not compilable.
    std::optional<JitCompiled> compile_sequence(const Memory& mem, uint64_t pc);
};
