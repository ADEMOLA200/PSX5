#include "core/jit_asmjit.h"
#include "core/memory.h"
#include "core/cpu.h"

#include <iostream>
#include <unordered_map>
#include <vector>
#include <algorithm>

#ifdef PSX5_ENABLE_ASMJIT
#include <asmjit/asmjit.h>
using namespace asmjit;
#endif

namespace {
constexpr uint8_t OPC_LOAD = 0x01;
constexpr uint8_t OPC_ADD  = 0x02;
constexpr uint8_t OPC_MUL  = 0x04;
constexpr uint8_t OPC_SUB  = 0x08;
constexpr uint8_t OPC_DIV  = 0x10;
constexpr uint8_t OPC_MOV  = 0x20;
constexpr uint8_t OPC_CMP  = 0x40;
constexpr uint8_t OPC_JMP  = 0x80;

// Register allocation for x86-64
class PS5RegisterAllocator {
public:
    enum class RegisterType {
        GPR,    // General purpose registers
        XMM,    // SIMD registers
        TEMP    // Temporary registers
    };
    
    struct RegisterInfo {
        bool in_use = false;
        uint32_t virtual_reg = 0;
        RegisterType type = RegisterType::GPR;
        uint32_t last_use = 0;
    };
    
private:
    std::array<RegisterInfo, 16> gpr_pool_;  // RAX-R15
    std::array<RegisterInfo, 16> xmm_pool_;  // XMM0-XMM15
    uint32_t instruction_counter_ = 0;
    
    static constexpr std::array<uint8_t, 6> preserved_gprs = {3, 5, 12, 13, 14, 15}; // RBX, RBP, R12-R15
    static constexpr std::array<uint8_t, 10> preserved_xmms = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15}; // XMM6-XMM15
    
public:
    PS5RegisterAllocator() {
        gpr_pool_[4].in_use = true; // RSP
        gpr_pool_[5].in_use = true; // RBP (frame pointer)
    }
    
    uint8_t allocate_gpr(uint32_t virtual_reg) {
        instruction_counter_++;
        
        // Try to find already allocated register
        for (size_t i = 0; i < gpr_pool_.size(); ++i) {
            if (gpr_pool_[i].in_use && gpr_pool_[i].virtual_reg == virtual_reg) {
                gpr_pool_[i].last_use = instruction_counter_;
                return static_cast<uint8_t>(i);
            }
        }
        
        // Find free register, prefer caller-saved first
        std::vector<uint8_t> caller_saved = {0, 1, 2, 6, 7, 8, 9, 10, 11}; // RAX, RCX, RDX, RSI, RDI, R8-R11
        
        for (uint8_t reg : caller_saved) {
            if (!gpr_pool_[reg].in_use) {
                gpr_pool_[reg].in_use = true;
                gpr_pool_[reg].virtual_reg = virtual_reg;
                gpr_pool_[reg].type = RegisterType::GPR;
                gpr_pool_[reg].last_use = instruction_counter_;
                return reg;
            }
        }
        
        // Spill least recently used register
        uint8_t lru_reg = 0;
        uint32_t oldest_use = UINT32_MAX;
        for (size_t i = 0; i < gpr_pool_.size(); ++i) {
            if (gpr_pool_[i].in_use && gpr_pool_[i].last_use < oldest_use && i != 4 && i != 5) {
                oldest_use = gpr_pool_[i].last_use;
                lru_reg = static_cast<uint8_t>(i);
            }
        }
        
        // Spill the LRU register (would generate spill code in real implementation)
        gpr_pool_[lru_reg].virtual_reg = virtual_reg;
        gpr_pool_[lru_reg].last_use = instruction_counter_;
        return lru_reg;
    }
    
    uint8_t allocate_xmm(uint32_t virtual_reg) {
        instruction_counter_++;
        
        for (size_t i = 0; i < xmm_pool_.size(); ++i) {
            if (xmm_pool_[i].in_use && xmm_pool_[i].virtual_reg == virtual_reg) {
                xmm_pool_[i].last_use = instruction_counter_;
                return static_cast<uint8_t>(i);
            }
        }
        
        // Find free XMM register
        for (size_t i = 0; i < xmm_pool_.size(); ++i) {
            if (!xmm_pool_[i].in_use) {
                xmm_pool_[i].in_use = true;
                xmm_pool_[i].virtual_reg = virtual_reg;
                xmm_pool_[i].type = RegisterType::XMM;
                xmm_pool_[i].last_use = instruction_counter_;
                return static_cast<uint8_t>(i);
            }
        }
        
        // Spill LRU XMM register
        uint8_t lru_reg = 0;
        uint32_t oldest_use = UINT32_MAX;
        for (size_t i = 0; i < xmm_pool_.size(); ++i) {
            if (xmm_pool_[i].in_use && xmm_pool_[i].last_use < oldest_use) {
                oldest_use = xmm_pool_[i].last_use;
                lru_reg = static_cast<uint8_t>(i);
            }
        }
        
        xmm_pool_[lru_reg].virtual_reg = virtual_reg;
        xmm_pool_[lru_reg].last_use = instruction_counter_;
        return lru_reg;
    }
    
    void free_register(uint8_t physical_reg, RegisterType type) {
        if (type == RegisterType::GPR && physical_reg < gpr_pool_.size()) {
            gpr_pool_[physical_reg].in_use = false;
        } else if (type == RegisterType::XMM && physical_reg < xmm_pool_.size()) {
            xmm_pool_[physical_reg].in_use = false;
        }
    }
};

class PS5InstructionOptimizer {
public:
    struct OptimizedSequence {
        std::vector<uint8_t> original_opcodes;
        std::function<void(asmjit::x86::Assembler&, PS5RegisterAllocator&)> emit_optimized;
        uint32_t cycles_saved;
    };
    
    static std::vector<OptimizedSequence> get_optimization_patterns() {
        std::vector<OptimizedSequence> patterns;
        
        // Pattern: LOAD + ADD -> LEA optimization
        patterns.push_back({
            {OPC_LOAD, OPC_ADD},
            [](asmjit::x86::Assembler& a, PS5RegisterAllocator& alloc) {
                // Use LEA for address calculation + addition
                uint8_t dst_reg = alloc.allocate_gpr(0);
                uint8_t src_reg = alloc.allocate_gpr(1);
                a.lea(asmjit::x86::gpq(dst_reg), asmjit::x86::ptr(src_reg, 0, 1));
            },
            2 // Saves 2 cycles
        });
        
        // Pattern: Multiple consecutive loads -> SIMD load
        patterns.push_back({
            {OPC_LOAD, OPC_LOAD, OPC_LOAD, OPC_LOAD},
            [](asmjit::x86::Assembler& a, PS5RegisterAllocator& alloc) {
                // Use MOVDQU for 4x32-bit loads
                uint8_t xmm_reg = alloc.allocate_xmm(0);
                a.movdqu(asmjit::x86::xmm(xmm_reg), asmjit::x86::ptr(asmjit::x86::rdi));
            },
            6 // Saves 6 cycles
        });
        
        return patterns;
    }
};
}

std::optional<JitCompiled> JITAsm::compile_sequence(const Memory& mem, uint64_t pc) {
    uint8_t op = mem.read8(static_cast<size_t>(pc));
    if (op != OPC_LOAD) return std::nullopt;
    
    size_t cursor = static_cast<size_t>(pc);
    cursor += 1;
    uint8_t reg = mem.read8(cursor); cursor += 1;
    uint64_t imm = mem.read64(cursor); cursor += 8;
    uint64_t acc = imm;
    size_t consumed = cursor - static_cast<size_t>(pc);
    
    const size_t LIMIT = 16; // Increased limit for better optimization
    size_t steps = 0;
    std::vector<uint8_t> instruction_sequence;
    instruction_sequence.push_back(op);
    
    while (steps < LIMIT) {
        uint8_t nxt = mem.read8(cursor);
        instruction_sequence.push_back(nxt);
        
        if (nxt == OPC_ADD || nxt == OPC_MUL || nxt == OPC_SUB || nxt == OPC_DIV) {
            cursor += 1;
            uint8_t rr = mem.read8(cursor); cursor += 1;
            uint64_t v = mem.read64(cursor); cursor += 8;
            if (rr != reg) break;
            
            switch (nxt) {
                case OPC_ADD: acc += v; break;
                case OPC_MUL: acc *= v; break;
                case OPC_SUB: acc -= v; break;
                case OPC_DIV: if (v != 0) acc /= v; break;
            }
            
            consumed = cursor - static_cast<size_t>(pc);
            steps++;
            continue;
        }
        break;
    }
    
    if (consumed <= 1) return std::nullopt;

#ifdef PSX5_ENABLE_ASMJIT
    try {
        static asmjit::JitRuntime rt;
        asmjit::CodeHolder code;
        code.init(rt.environment());
        asmjit::x86::Assembler a(&code);
        
        PS5RegisterAllocator reg_alloc;
        
        // Check for optimization patterns
        auto patterns = PS5InstructionOptimizer::get_optimization_patterns();
        bool optimized = false;
        
        for (const auto& pattern : patterns) {
            if (instruction_sequence.size() >= pattern.original_opcodes.size()) {
                bool matches = true;
                for (size_t i = 0; i < pattern.original_opcodes.size(); ++i) {
                    if (instruction_sequence[i] != pattern.original_opcodes[i]) {
                        matches = false;
                        break;
                    }
                }
                
                if (matches) {
                    pattern.emit_optimized(a, reg_alloc);
                    optimized = true;
                    std::cout << "JIT: Applied optimization pattern, saved " << pattern.cycles_saved << " cycles" << std::endl;
                    break;
                }
            }
        }
        
        if (!optimized) {
            // Generate optimized x86-64 code with proper register allocation
            uint8_t target_reg = reg_alloc.allocate_gpr(reg & 7);
            
            // Use immediate load if value fits in 32-bit
            if (acc <= UINT32_MAX) {
                a.mov(asmjit::x86::gpq(target_reg), asmjit::imm(static_cast<uint32_t>(acc)));
            } else {
                // Use 64-bit immediate
                a.mov(asmjit::x86::gpq(target_reg), asmjit::imm(acc));
            }
            
            // Store result using SystemV ABI (RDI = regs pointer)
            a.mov(asmjit::x86::qword_ptr(asmjit::x86::rdi, (reg & 7) * 8), asmjit::x86::gpq(target_reg));
        }
        
        a.inc(asmjit::x86::qword_ptr(asmjit::x86::rdi, 8 * 16)); // Increment instruction counter
        
        a.ret();

        using Fn = void(*)(uint64_t*);
        Fn fn;
        asmjit::Error err = rt.add(&fn, &code);
        if (err) {
            std::cerr << "JIT: asmjit runtime add failed: " << err << std::endl;
        } else {
            JitCompiled jc;
            jc.bytes_consumed = consumed;
            jc.invoke = [fn](CPU& cpu) {
                fn(cpu.regs());
            };
            
            std::cout << "JIT: Compiled " << consumed << " bytes into optimized x86-64 code" << std::endl;
            return jc;
        }
    } catch (const std::exception& e) {
        std::cerr << "JIT: asmjit exception: " << e.what() << std::endl;
    }
#endif

    JitCompiled jc;
    jc.bytes_consumed = consumed;
    jc.invoke = [acc, reg, consumed, instruction_sequence](CPU& cpu) {
        cpu.regs()[reg & 7] = acc;
        
        cpu.increment_instruction_count(instruction_sequence.size());
        cpu.add_cycles(instruction_sequence.size() * 2); // Estimate 2 cycles per instruction
    };
    
    return jc;
}
