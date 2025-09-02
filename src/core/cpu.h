#pragma once
#include "memory.h"
#include "syscalls.h"
#include <cstdint>
#include <array>
#include <unordered_map>
#include <functional>

// AMD Zen CPU emulation with x86-64 instruction set support
// Registers: RAX, RBX, RCX, RDX, RSI, RDI, RSP, RBP, R8-R15 (64-bit)
// XMM0-XMM15 (128-bit SIMD), YMM0-YMM15 (256-bit AVX)
// Control registers: CR0, CR3, CR4, EFER
// Segment registers: CS, DS, ES, FS, GS, SS

struct CPUState {
    // General purpose registers (64-bit)
    union {
        uint64_t gpr[16];
        struct {
            uint64_t rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi;
            uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
        };
    };
    
    // SIMD registers
    __m128i xmm[16];  // XMM0-XMM15
    __m256i ymm[16];  // YMM0-YMM15 (AVX)
    
    // Control registers
    uint64_t cr0, cr3, cr4, efer;
    
    // Segment registers
    uint16_t cs, ds, es, fs, gs, ss;
    
    // Flags register
    uint64_t rflags;
    
    // Program counter
    uint64_t rip;
    
    // Privilege level (0-3)
    uint8_t cpl;
    
    // Paging enabled
    bool paging_enabled;
};

class CPU {
public:
    void invalidate_code_at(size_t addr, size_t len);
    explicit CPU(Memory& mem, Syscalls& sc);
    void reset(uint64_t pc_start);
    void step();
    bool running() const { return running_; }
    uint64_t pc() const { return state_.rip; }
    uint64_t* regs() { return state_.gpr; }
    CPUState& get_state() { return state_; }

    void handle_interrupt(uint8_t vector);
    void set_privilege_level(uint8_t level);
    bool check_privilege(uint8_t required_level);
    void enable_paging(uint64_t page_table_base);
    uint64_t translate_address(uint64_t virtual_addr);
    
    void apply_jit_result(size_t reg_index, uint64_t value, size_t bytes_consumed);
    void compile_and_cache_block(uint64_t start_addr);

    // Helpers for tests
    void run_steps(size_t n);
    
private:
    struct Instruction {
        uint8_t opcode;
        uint8_t modrm;
        uint8_t sib;
        uint8_t rex;
        uint32_t displacement;
        uint64_t immediate;
        uint8_t length;
    };
    
    Instruction decode_instruction(uint64_t addr);
    void execute_instruction(const Instruction& instr);
    
    void handle_mov(const Instruction& instr);
    void handle_add(const Instruction& instr);
    void handle_sub(const Instruction& instr);
    void handle_mul(const Instruction& instr);
    void handle_div(const Instruction& instr);
    void handle_jmp(const Instruction& instr);
    void handle_call(const Instruction& instr);
    void handle_ret(const Instruction& instr);
    void handle_push(const Instruction& instr);
    void handle_pop(const Instruction& instr);
    void handle_cmp(const Instruction& instr);
    void handle_test(const Instruction& instr);
    void handle_conditional_jump(const Instruction& instr);
    void handle_simd_instruction(const Instruction& instr);
    void handle_system_instruction(const Instruction& instr);
    
    std::unordered_map<uint64_t, std::function<void(CPU&)>> code_cache_;
    
    Memory& mem_;
    Syscalls& sys_;
    CPUState state_;
    bool running_{false};
    
    std::array<uint64_t, 256> interrupt_table_;
    bool interrupts_enabled_{true};
};
