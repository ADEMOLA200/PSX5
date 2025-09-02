#include "cpu.h"
#include "jit.h"
#include <optional>
#include <immintrin.h>

namespace {
enum X86Opcode : uint16_t {
    // Basic arithmetic
    MOV_REG_IMM = 0xB8,    // MOV reg, imm32/64
    MOV_REG_REG = 0x89,    // MOV reg, reg
    MOV_MEM_REG = 0x89,    // MOV [mem], reg
    MOV_REG_MEM = 0x8B,    // MOV reg, [mem]
    MOV_IMM_MEM = 0xC7,    // MOV [mem], imm
    
    ADD_REG_IMM = 0x81,    // ADD reg, imm
    ADD_REG_REG = 0x01,    // ADD reg, reg
    ADD_REG_MEM = 0x03,    // ADD reg, [mem]
    SUB_REG_IMM = 0x81,    // SUB reg, imm
    SUB_REG_REG = 0x29,    // SUB reg, reg
    SUB_REG_MEM = 0x2B,    // SUB reg, [mem]
    
    AND_REG_REG = 0x21,    // AND reg, reg
    AND_REG_IMM = 0x81,    // AND reg, imm
    OR_REG_REG = 0x09,     // OR reg, reg
    OR_REG_IMM = 0x81,     // OR reg, imm
    XOR_REG_REG = 0x31,    // XOR reg, reg
    XOR_REG_IMM = 0x81,    // XOR reg, imm
    NOT_REG = 0xF7,        // NOT reg
    NEG_REG = 0xF7,        // NEG reg
    
    // Shift and rotate operations
    SHL_REG_IMM = 0xC1,    // SHL reg, imm8
    SHL_REG_CL = 0xD3,     // SHL reg, CL
    SHR_REG_IMM = 0xC1,    // SHR reg, imm8
    SHR_REG_CL = 0xD3,     // SHR reg, CL
    SAR_REG_IMM = 0xC1,    // SAR reg, imm8
    SAR_REG_CL = 0xD3,     // SAR reg, CL
    ROL_REG_IMM = 0xC1,    // ROL reg, imm8
    ROR_REG_IMM = 0xC1,    // ROR reg, imm8
    
    // Increment/Decrement
    INC_REG = 0xFF,        // INC reg
    DEC_REG = 0xFF,        // DEC reg
    INC_MEM = 0xFF,        // INC [mem]
    DEC_MEM = 0xFF,        // DEC [mem]
    
    // Multiplication/Division
    MUL_REG = 0xF7,        // MUL reg
    IMUL_REG = 0xF7,       // IMUL reg
    IMUL_REG_REG = 0x0FAF, // IMUL reg, reg
    IMUL_REG_IMM = 0x69,   // IMUL reg, imm
    DIV_REG = 0xF7,        // DIV reg
    IDIV_REG = 0xF7,       // IDIV reg
    
    // Control flow
    JMP_REL32 = 0xE9,      // JMP rel32
    JMP_REL8 = 0xEB,       // JMP rel8
    JMP_REG = 0xFF,        // JMP reg
    JMP_MEM = 0xFF,        // JMP [mem]
    CALL_REL32 = 0xE8,     // CALL rel32
    CALL_REG = 0xFF,       // CALL reg
    CALL_MEM = 0xFF,       // CALL [mem]
    RET_NEAR = 0xC3,       // RET
    RET_FAR = 0xCB,        // RETF
    RET_IMM = 0xC2,        // RET imm16
    
    // Conditional jumps (all 16 conditions)
    JO_REL32 = 0x0F80,     // JO rel32
    JNO_REL32 = 0x0F81,    // JNO rel32
    JB_REL32 = 0x0F82,     // JB/JC rel32
    JNB_REL32 = 0x0F83,    // JNB/JNC rel32
    JE_REL32 = 0x0F84,     // JE/JZ rel32
    JNE_REL32 = 0x0F85,    // JNE/JNZ rel32
    JBE_REL32 = 0x0F86,    // JBE/JNA rel32
    JA_REL32 = 0x0F87,     // JA/JNBE rel32
    JS_REL32 = 0x0F88,     // JS rel32
    JNS_REL32 = 0x0F89,    // JNS rel32
    JP_REL32 = 0x0F8A,     // JP/JPE rel32
    JNP_REL32 = 0x0F8B,    // JNP/JPO rel32
    JL_REL32 = 0x0F8C,     // JL/JNGE rel32
    JGE_REL32 = 0x0F8D,    // JGE/JNL rel32
    JLE_REL32 = 0x0F8E,    // JLE/JNG rel32
    JG_REL32 = 0x0F8F,     // JG/JNLE rel32
    
    // Stack operations
    PUSH_REG = 0x50,       // PUSH reg
    POP_REG = 0x58,        // POP reg
    PUSH_IMM = 0x68,       // PUSH imm32
    PUSH_IMM8 = 0x6A,      // PUSH imm8
    PUSH_MEM = 0xFF,       // PUSH [mem]
    POP_MEM = 0x8F,        // POP [mem]
    PUSHF = 0x9C,          // PUSHF
    POPF = 0x9D,           // POPF
    PUSHA = 0x60,          // PUSHA (32-bit mode)
    POPA = 0x61,           // POPA (32-bit mode)
    
    // Comparison and testing
    CMP_REG_IMM = 0x81,    // CMP reg, imm
    CMP_REG_REG = 0x39,    // CMP reg, reg
    CMP_REG_MEM = 0x3B,    // CMP reg, [mem]
    CMP_MEM_REG = 0x39,    // CMP [mem], reg
    TEST_REG_REG = 0x85,   // TEST reg, reg
    TEST_REG_IMM = 0xF7,   // TEST reg, imm
    TEST_MEM_REG = 0x85,   // TEST [mem], reg
    
    // String operations
    MOVS = 0xA4,           // MOVS
    MOVSW = 0xA5,          // MOVSW/MOVSD/MOVSQ
    CMPS = 0xA6,           // CMPS
    CMPSW = 0xA7,          // CMPSW/CMPSD/CMPSQ
    SCAS = 0xAE,           // SCAS
    SCASW = 0xAF,          // SCASW/SCASD/SCASQ
    LODS = 0xAC,           // LODS
    LODSW = 0xAD,          // LODSW/LODSD/LODSQ
    STOS = 0xAA,           // STOS
    STOSW = 0xAB,          // STOSW/STOSD/STOSQ
    
    // Conditional moves (CMOVcc)
    CMOVO = 0x0F40,        // CMOVO
    CMOVNO = 0x0F41,       // CMOVNO
    CMOVB = 0x0F42,        // CMOVB/CMOVC
    CMOVNB = 0x0F43,       // CMOVNB/CMOVNC
    CMOVE = 0x0F44,        // CMOVE/CMOVZ
    CMOVNE = 0x0F45,       // CMOVNE/CMOVNZ
    CMOVBE = 0x0F46,       // CMOVBE/CMOVNA
    CMOVA = 0x0F47,        // CMOVA/CMOVNBE
    CMOVS = 0x0F48,        // CMOVS
    CMOVNS = 0x0F49,       // CMOVNS
    CMOVP = 0x0F4A,        // CMOVP/CMOVPE
    CMOVNP = 0x0F4B,       // CMOVNP/CMOVPO
    CMOVL = 0x0F4C,        // CMOVL/CMOVNGE
    CMOVGE = 0x0F4D,       // CMOVGE/CMOVNL
    CMOVLE = 0x0F4E,       // CMOVLE/CMOVNG
    CMOVG = 0x0F4F,        // CMOVG/CMOVNLE
    
    // Bit manipulation
    BSF = 0x0FBC,          // BSF (Bit Scan Forward)
    BSR = 0x0FBD,          // BSR (Bit Scan Reverse)
    BT = 0x0FA3,           // BT (Bit Test)
    BTC = 0x0FBB,          // BTC (Bit Test and Complement)
    BTR = 0x0FB3,          // BTR (Bit Test and Reset)
    BTS = 0x0FAB,          // BTS (Bit Test and Set)
    
    // Load effective address
    LEA = 0x8D,            // LEA reg, [mem]
    
    // Exchange operations
    XCHG_REG_REG = 0x87,   // XCHG reg, reg
    XCHG_MEM_REG = 0x87,   // XCHG [mem], reg
    XADD = 0x0FC1,         // XADD reg/mem, reg
    CMPXCHG = 0x0FB1,      // CMPXCHG reg/mem, reg
    CMPXCHG8B = 0x0FC7,    // CMPXCHG8B [mem]
    
    // System instructions
    SYSCALL = 0x0F05,      // SYSCALL
    SYSRET = 0x0F07,       // SYSRET
    SYSENTER = 0x0F34,     // SYSENTER
    SYSEXIT = 0x0F35,      // SYSEXIT
    INT = 0xCD,            // INT imm8
    INT3 = 0xCC,           // INT3
    INTO = 0xCE,           // INTO
    IRET = 0xCF,           // IRET
    HLT = 0xF4,            // HLT
    NOP = 0x90,            // NOP
    
    // Flag operations
    CLC = 0xF8,            // CLC (Clear Carry)
    STC = 0xF9,            // STC (Set Carry)
    CLI = 0xFA,            // CLI (Clear Interrupt)
    STI = 0xFB,            // STI (Set Interrupt)
    CLD = 0xFC,            // CLD (Clear Direction)
    STD = 0xFD,            // STD (Set Direction)
    CMC = 0xF5,            // CMC (Complement Carry)
    
    // Control register operations
    MOV_CR_REG = 0x0F22,   // MOV CR, reg
    MOV_REG_CR = 0x0F20,   // MOV reg, CR
    MOV_DR_REG = 0x0F23,   // MOV DR, reg
    MOV_REG_DR = 0x0F21,   // MOV reg, DR
    
    // SIMD instructions (SSE/AVX)
    MOVDQA_XMM = 0x660F6F, // MOVDQA xmm, xmm/m128
    MOVDQU_XMM = 0xF30F6F, // MOVDQU xmm, xmm/m128
    MOVAPS_XMM = 0x0F28,   // MOVAPS xmm, xmm/m128
    MOVUPS_XMM = 0x0F10,   // MOVUPS xmm, xmm/m128
    PADDD_XMM = 0x660FFE,  // PADDD xmm, xmm/m128
    PSUBD_XMM = 0x660FFA,  // PSUBD xmm, xmm/m128
    PMULLD_XMM = 0x660F3840, // PMULLD xmm, xmm/m128
    ADDPS_XMM = 0x0F58,    // ADDPS xmm, xmm/m128
    SUBPS_XMM = 0x0F5C,    // SUBPS xmm, xmm/m128
    MULPS_XMM = 0x0F59,    // MULPS xmm, xmm/m128
    DIVPS_XMM = 0x0F5E,    // DIVPS xmm, xmm/m128
    
    // Set byte on condition
    SETO = 0x0F90,         // SETO
    SETNO = 0x0F91,        // SETNO
    SETB = 0x0F92,         // SETB/SETC
    SETNB = 0x0F93,        // SETNB/SETNC
    SETE = 0x0F94,         // SETE/SETZ
    SETNE = 0x0F95,        // SETNE/SETNZ
    SETBE = 0x0F96,        // SETBE/SETNA
    SETA = 0x0F97,         // SETA/SETNBE
    SETS = 0x0F98,         // SETS
    SETNS = 0x0F99,        // SETNS
    SETP = 0x0F9A,         // SETP/SETPE
    SETNP = 0x0F9B,        // SETNP/SETPO
    SETL = 0x0F9C,         // SETL/SETNGE
    SETGE = 0x0F9D,        // SETGE/SETNL
    SETLE = 0x0F9E,        // SETLE/SETNG
    SETG = 0x0F9F,         // SETG/SETNLE
    
    // Loop instructions
    LOOP = 0xE2,           // LOOP
    LOOPE = 0xE1,          // LOOPE/LOOPZ
    LOOPNE = 0xE0,         // LOOPNE/LOOPNZ
    JCXZ = 0xE3,           // JCXZ/JECXZ/JRCXZ
    
    // Processor identification
    CPUID = 0x0FA2,        // CPUID
    
    // Time stamp counter
    RDTSC = 0x0F31,        // RDTSC
    RDTSCP = 0x0F01F9,     // RDTSCP
    
    // Cache control
    PREFETCH = 0x0F18,     // PREFETCH
    CLFLUSH = 0x0FAE,      // CLFLUSH
    
    // Memory barriers
    MFENCE = 0x0FAEF0,     // MFENCE
    SFENCE = 0x0FAEF8,     // SFENCE
    LFENCE = 0x0FAEE8,     // LFENCE
};

enum X86Register : uint8_t {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8 = 8, R9 = 9, R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15
};

enum RFlags : uint64_t {
    CF = 1 << 0,   // Carry Flag
    PF = 1 << 2,   // Parity Flag
    AF = 1 << 4,   // Auxiliary Carry Flag
    ZF = 1 << 6,   // Zero Flag
    SF = 1 << 7,   // Sign Flag
    TF = 1 << 8,   // Trap Flag
    IF = 1 << 9,   // Interrupt Enable Flag
    DF = 1 << 10,  // Direction Flag
    OF = 1 << 11,  // Overflow Flag
};

bool instruction_has_modrm(uint16_t opcode) {
    // Single-byte opcodes that don't have ModR/M
    if ((opcode & 0xFF00) == 0) {
        uint8_t op = opcode & 0xFF;
        // Push/Pop register opcodes (0x50-0x5F)
        if (op >= 0x50 && op <= 0x5F) return false;
        // MOV reg, imm opcodes (0xB0-0xBF)
        if (op >= 0xB0 && op <= 0xBF) return false;
        // Single byte instructions
        switch (op) {
            case 0x90: // NOP
            case 0xC3: // RET
            case 0xCB: // RETF
            case 0xCC: // INT3
            case 0xCD: // INT imm8
            case 0xCE: // INTO
            case 0xCF: // IRET
            case 0xF4: // HLT
            case 0xF5: // CMC
            case 0xF8: // CLC
            case 0xF9: // STC
            case 0xFA: // CLI
            case 0xFB: // STI
            case 0xFC: // CLD
            case 0xFD: // STD
                return false;
        }
    }
    
    // Two-byte opcodes (0x0F prefix)
    if ((opcode & 0xFF00) == 0x0F00) {
        uint8_t second_byte = opcode & 0xFF;
        switch (second_byte) {
            case 0x05: // SYSCALL
            case 0x07: // SYSRET
            case 0x08: // INVD
            case 0x09: // WBINVD
            case 0x0B: // UD2
            case 0x30: // WRMSR
            case 0x31: // RDTSC
            case 0x32: // RDMSR
            case 0x33: // RDPMC
            case 0x34: // SYSENTER
            case 0x35: // SYSEXIT
                return false;
        }
    }
    
    // Most other instructions have ModR/M
    return true;
}

uint64_t read_u64_from_mem(const class Memory& mem, uint64_t addr){
    return mem.read64(static_cast<size_t>(addr));
}

void write_u64_to_mem(class Memory& mem, uint64_t addr, uint64_t value){
    mem.write64(static_cast<size_t>(addr), value);
}
}

CPU::CPU(Memory& mem, Syscalls& sc) : mem_(mem), sys_(sc) {
    memset(&state_, 0, sizeof(state_));
    
    // Initialize interrupt table with default handlers
    for(int i = 0; i < 256; i++) {
        interrupt_table_[i] = 0; // Default to null handler
    }
}

void CPU::reset(uint64_t pc_start){
    memset(&state_, 0, sizeof(state_));
    state_.rip = pc_start;
    state_.rsp = 0x7FFFFFFFFFFF; // Set stack pointer to high memory
    state_.cs = 0x08; // Code segment
    state_.ds = state_.es = state_.fs = state_.gs = state_.ss = 0x10; // Data segments
    state_.cr0 = 0x60000010; // Enable protected mode
    state_.rflags = 0x202; // Enable interrupts
    state_.cpl = 0; // Ring 0 (kernel mode)
    running_ = true;
}

CPU::Instruction CPU::decode_instruction(uint64_t addr) {
    Instruction instr = {};
    uint8_t* code = reinterpret_cast<uint8_t*>(mem_.get_ptr(addr));
    size_t offset = 0;
    
    // Parse legacy prefixes
    bool done_prefixes = false;
    while (!done_prefixes && offset < 15) { // Max 15 bytes per instruction
        switch (code[offset]) {
            case 0xF0: // LOCK
                instr.prefixes |= PREFIX_LOCK;
                offset++;
                break;
            case 0xF2: // REPNE/REPNZ
                instr.prefixes |= PREFIX_REPNE;
                offset++;
                break;
            case 0xF3: // REP/REPE/REPZ
                instr.prefixes |= PREFIX_REP;
                offset++;
                break;
            case 0x2E: // CS override
            case 0x36: // SS override
            case 0x3E: // DS override
            case 0x26: // ES override
            case 0x64: // FS override
            case 0x65: // GS override
                instr.segment_override = code[offset];
                offset++;
                break;
            case 0x66: // Operand size override
                instr.prefixes |= PREFIX_OPERAND_SIZE;
                offset++;
                break;
            case 0x67: // Address size override
                instr.prefixes |= PREFIX_ADDRESS_SIZE;
                offset++;
                break;
            default:
                done_prefixes = true;
                break;
        }
    }
    
    // Parse REX prefix (64-bit mode only)
    if (code[offset] >= 0x40 && code[offset] <= 0x4F) {
        instr.rex = code[offset];
        offset++;
    }
    
    // Parse opcode
    instr.opcode = code[offset];
    offset++;
    
    // Handle two-byte and three-byte opcodes
    if (instr.opcode == 0x0F) {
        instr.opcode = (instr.opcode << 8) | code[offset];
        offset++;
        
        // Check for three-byte opcodes
        if ((code[offset-1] == 0x38) || (code[offset-1] == 0x3A)) {
            instr.opcode = (instr.opcode << 8) | code[offset];
            offset++;
        }
    }
    
    bool has_modrm = instruction_has_modrm(instr.opcode);
    if (has_modrm) {
        instr.modrm = code[offset];
        offset++;
        
        uint8_t mod = (instr.modrm >> 6) & 3;
        uint8_t rm = instr.modrm & 7;
        
        // Parse SIB byte if needed
        if (mod != 3 && rm == 4) {
            instr.sib = code[offset];
            offset++;
        }
        
        // Parse displacement
        if (mod == 1) {
            // 8-bit displacement
            instr.displacement = (int8_t)code[offset];
            offset++;
        } else if (mod == 2 || (mod == 0 && rm == 5)) {
            // 32-bit displacement
            instr.displacement = *(int32_t*)&code[offset];
            offset += 4;
        }
    }
    
    // Parse immediate operand
    switch (instr.opcode) {
        case MOV_REG_IMM:
        case MOV_REG_IMM + 1:
        case MOV_REG_IMM + 2:
        case MOV_REG_IMM + 3:
        case MOV_REG_IMM + 4:
        case MOV_REG_IMM + 5:
        case MOV_REG_IMM + 6:
        case MOV_REG_IMM + 7:
            // 64-bit immediate for REX.W, 32-bit otherwise
            if (instr.rex & 0x08) {
                instr.immediate = *(uint64_t*)&code[offset];
                offset += 8;
            } else {
                instr.immediate = *(uint32_t*)&code[offset];
                offset += 4;
            }
            break;
        case JMP_REL32:
        case CALL_REL32:
        case JE_REL32:
        case JNE_REL32:
        case JL_REL32:
        case JG_REL32:
            instr.immediate = *(int32_t*)&code[offset];
            offset += 4;
            break;
        case JMP_REL8:
            instr.immediate = (int8_t)code[offset];
            offset += 1;
            break;
        case PUSH_IMM:
            instr.immediate = *(int32_t*)&code[offset];
            offset += 4;
            break;
        case INT:
            instr.immediate = code[offset];
            offset += 1;
            break;
    }
    
    instr.length = offset;
    return instr;
}

void CPU::step(){
    if(!running_) return;
    
    Instruction instr = decode_instruction(state_.rip);
    execute_instruction(instr);
    state_.rip += instr.length;
}

void CPU::execute_instruction(const Instruction& instr) {
    switch(instr.opcode) {
        // Basic MOV operations
        case MOV_REG_REG:
        case MOV_REG_MEM:
        case MOV_MEM_REG:
        case MOV_IMM_MEM:
            handle_mov(instr);
            break;
            
        // Arithmetic operations
        case ADD_REG_REG:
        case ADD_REG_IMM:
        case ADD_REG_MEM:
            handle_add(instr);
            break;
        case SUB_REG_REG:
        case SUB_REG_IMM:
        case SUB_REG_MEM:
            handle_sub(instr);
            break;
            
        case AND_REG_REG:
        case AND_REG_IMM:
            handle_and(instr);
            break;
        case OR_REG_REG:
        case OR_REG_IMM:
            handle_or(instr);
            break;
        case XOR_REG_REG:
        case XOR_REG_IMM:
            handle_xor(instr);
            break;
        case NOT_REG:
            handle_not(instr);
            break;
        case NEG_REG:
            handle_neg(instr);
            break;
            
        // Shift and rotate operations
        case SHL_REG_IMM:
        case SHL_REG_CL:
            handle_shl(instr);
            break;
        case SHR_REG_IMM:
        case SHR_REG_CL:
            handle_shr(instr);
            break;
        case SAR_REG_IMM:
        case SAR_REG_CL:
            handle_sar(instr);
            break;
        case ROL_REG_IMM:
            handle_rol(instr);
            break;
        case ROR_REG_IMM:
            handle_ror(instr);
            break;
            
        // Increment/Decrement
        case INC_REG:
        case INC_MEM:
            handle_inc(instr);
            break;
        case DEC_REG:
        case DEC_MEM:
            handle_dec(instr);
            break;
            
        // Multiplication/Division
        case MUL_REG:
        case IMUL_REG:
        case IMUL_REG_REG:
        case IMUL_REG_IMM:
            handle_mul(instr);
            break;
        case DIV_REG:
        case IDIV_REG:
            handle_div(instr);
            break;
            
        // Control flow
        case JMP_REL32:
        case JMP_REL8:
        case JMP_REG:
        case JMP_MEM:
            handle_jmp(instr);
            break;
        case CALL_REL32:
        case CALL_REG:
        case CALL_MEM:
            handle_call(instr);
            break;
        case RET_NEAR:
        case RET_FAR:
        case RET_IMM:
            handle_ret(instr);
            break;
            
        // Stack operations
        case PUSH_REG:
        case PUSH_IMM:
        case PUSH_IMM8:
        case PUSH_MEM:
            handle_push(instr);
            break;
        case POP_REG:
        case POP_MEM:
            handle_pop(instr);
            break;
        case PUSHF:
            handle_pushf(instr);
            break;
        case POPF:
            handle_popf(instr);
            break;
            
        // Comparison and testing
        case CMP_REG_REG:
        case CMP_REG_IMM:
        case CMP_REG_MEM:
        case CMP_MEM_REG:
            handle_cmp(instr);
            break;
        case TEST_REG_REG:
        case TEST_REG_IMM:
        case TEST_MEM_REG:
            handle_test(instr);
            break;
            
        // Conditional jumps
        case JO_REL32: case JNO_REL32: case JB_REL32: case JNB_REL32:
        case JE_REL32: case JNE_REL32: case JBE_REL32: case JA_REL32:
        case JS_REL32: case JNS_REL32: case JP_REL32: case JNP_REL32:
        case JL_REL32: case JGE_REL32: case JLE_REL32: case JG_REL32:
            handle_conditional_jump(instr);
            break;
            
        // Conditional moves
        case CMOVO: case CMOVNO: case CMOVB: case CMOVNB:
        case CMOVE: case CMOVNE: case CMOVBE: case CMOVA:
        case CMOVS: case CMOVNS: case CMOVP: case CMOVNP:
        case CMOVL: case CMOVGE: case CMOVLE: case CMOVG:
            handle_cmov(instr);
            break;
            
        // String operations
        case MOVS: case MOVSW: case CMPS: case CMPSW:
        case SCAS: case SCASW: case LODS: case LODSW:
        case STOS: case STOSW:
            handle_string_operation(instr);
            break;
            
        // Bit manipulation
        case BSF: case BSR: case BT: case BTC: case BTR: case BTS:
            handle_bit_operation(instr);
            break;
            
        // Load effective address
        case LEA:
            handle_lea(instr);
            break;
            
        // Exchange operations
        case XCHG_REG_REG:
        case XCHG_MEM_REG:
            handle_xchg(instr);
            break;
        case XADD:
            handle_xadd(instr);
            break;
        case CMPXCHG:
        case CMPXCHG8B:
            handle_cmpxchg(instr);
            break;
            
        // Set byte on condition
        case SETO: case SETNO: case SETB: case SETNB:
        case SETE: case SETNE: case SETBE: case SETA:
        case SETS: case SETNS: case SETP: case SETNP:
        case SETL: case SETGE: case SETLE: case SETG:
            handle_setcc(instr);
            break;
            
        // Loop instructions
        case LOOP: case LOOPE: case LOOPNE: case JCXZ:
            handle_loop(instr);
            break;
            
        // Flag operations
        case CLC: case STC: case CLI: case STI:
        case CLD: case STD: case CMC:
            handle_flag_operation(instr);
            break;
            
        // System instructions
        case SYSCALL:
            sys_.handle(0, state_.gpr, running_);
            break;
        case SYSRET:
            handle_sysret(instr);
            break;
        case INT:
        case INT3:
            handle_interrupt_instruction(instr);
            break;
        case IRET:
            handle_iret(instr);
            break;
        case HLT:
            running_ = false;
            break;
        case NOP:
            // Do nothing
            break;
            
        // Control register operations
        case MOV_CR_REG:
        case MOV_REG_CR:
        case MOV_DR_REG:
        case MOV_REG_DR:
            handle_system_instruction(instr);
            break;
            
        // SIMD instructions
        case MOVDQA_XMM: case MOVDQU_XMM: case MOVAPS_XMM: case MOVUPS_XMM:
        case PADDD_XMM: case PSUBD_XMM: case PMULLD_XMM:
        case ADDPS_XMM: case SUBPS_XMM: case MULPS_XMM: case DIVPS_XMM:
            handle_simd_instruction(instr);
            break;
            
        // Processor identification and timing
        case CPUID:
            handle_cpuid(instr);
            break;
        case RDTSC:
        case RDTSCP:
            handle_rdtsc(instr);
            break;
            
        // Cache and memory operations
        case PREFETCH:
        case CLFLUSH:
            handle_cache_operation(instr);
            break;
        case MFENCE:
        case SFENCE:
        case LFENCE:
            handle_memory_barrier(instr);
            break;
            
        default:
            // Unknown instruction - halt for safety
            std::cout << "CPU: Unknown instruction 0x" << std::hex << instr.opcode << std::dec << " at RIP 0x" << std::hex << state_.rip << std::dec << std::endl;
            running_ = false;
            break;
    }
}



void CPU::handle_and(const Instruction& instr) {
    uint8_t mod = (instr.modrm >> 6) & 3;
    uint8_t reg = (instr.modrm >> 3) & 7;
    uint8_t rm = instr.modrm & 7;
    
    if (instr.rex & 0x04) reg += 8;
    if (instr.rex & 0x01) rm += 8;
    
    uint64_t src_val, dst_val;
    
    if (instr.opcode == AND_REG_IMM) {
        dst_val = state_.gpr[rm];
        src_val = instr.immediate;
    } else {
        dst_val = state_.gpr[rm];
        src_val = state_.gpr[reg];
    }
    
    uint64_t result = dst_val & src_val;
    
    // Update flags
    state_.rflags &= ~(ZF | SF | CF | OF | PF);
    if (result == 0) state_.rflags |= ZF;
    if (result & (1ULL << 63)) state_.rflags |= SF;
    
    // Parity flag
    uint8_t low_byte = result & 0xFF;
    uint8_t parity = 0;
    for (int i = 0; i < 8; i++) {
        if (low_byte & (1 << i)) parity++;
    }
    if ((parity & 1) == 0) state_.rflags |= PF;
    
    state_.gpr[rm] = result;
}

void CPU::handle_or(const Instruction& instr) {
    uint8_t mod = (instr.modrm >> 6) & 3;
    uint8_t reg = (instr.modrm >> 3) & 7;
    uint8_t rm = instr.modrm & 7;
    
    if (instr.rex & 0x04) reg += 8;
    if (instr.rex & 0x01) rm += 8;
    
    uint64_t src_val, dst_val;
    
    if (instr.opcode == OR_REG_IMM) {
        dst_val = state_.gpr[rm];
        src_val = instr.immediate;
    } else {
        dst_val = state_.gpr[rm];
        src_val = state_.gpr[reg];
    }
    
    uint64_t result = dst_val | src_val;
    
    // Update flags
    state_.rflags &= ~(ZF | SF | CF | OF | PF);
    if (result == 0) state_.rflags |= ZF;
    if (result & (1ULL << 63)) state_.rflags |= SF;
    
    // Parity flag
    uint8_t low_byte = result & 0xFF;
    uint8_t parity = 0;
    for (int i = 0; i < 8; i++) {
        if (low_byte & (1 << i)) parity++;
    }
    if ((parity & 1) == 0) state_.rflags |= PF;
    
    state_.gpr[rm] = result;
}

void CPU::handle_xor(const Instruction& instr) {
    uint8_t mod = (instr.modrm >> 6) & 3;
    uint8_t reg = (instr.modrm >> 3) & 7;
    uint8_t rm = instr.modrm & 7;
    
    if (instr.rex & 0x04) reg += 8;
    if (instr.rex & 0x01) rm += 8;
    
    uint64_t src_val, dst_val;
    
    if (instr.opcode == XOR_REG_IMM) {
        dst_val = state_.gpr[rm];
        src_val = instr.immediate;
    } else {
        dst_val = state_.gpr[rm];
        src_val = state_.gpr[reg];
    }
    
    uint64_t result = dst_val ^ src_val;
    
    // Update flags
    state_.rflags &= ~(ZF | SF | CF | OF | PF);
    if (result == 0) state_.rflags |= ZF;
    if (result & (1ULL << 63)) state_.rflags |= SF;
    
    // Parity flag
    uint8_t low_byte = result & 0xFF;
    uint8_t parity = 0;
    for (int i = 0; i < 8; i++) {
        if (low_byte & (1 << i)) parity++;
    }
    if ((parity & 1) == 0) state_.rflags |= PF;
    
    state_.gpr[rm] = result;
}

void CPU::handle_not(const Instruction& instr) {
    uint8_t rm = instr.modrm & 7;
    if (instr.rex & 0x01) rm += 8;
    
    state_.gpr[rm] = ~state_.gpr[rm];
}

void CPU::handle_neg(const Instruction& instr) {
    uint8_t rm = instr.modrm & 7;
    if (instr.rex & 0x01) rm += 8;
    
    uint64_t value = state_.gpr[rm];
    uint64_t result = (~value) + 1;
    
    // Update flags
    state_.rflags &= ~(ZF | SF | CF | OF | PF | AF);
    if (result == 0) state_.rflags |= ZF;
    if (result & (1ULL << 63)) state_.rflags |= SF;
    if (value != 0) state_.rflags |= CF;
    if (value == 0x8000000000000000ULL) state_.rflags |= OF;
    
    state_.gpr[rm] = result;
}

void CPU::handle_shl(const Instruction& instr) {
    uint8_t rm = instr.modrm & 7;
    if (instr.rex & 0x01) rm += 8;
    
    uint8_t count;
    if (instr.opcode == SHL_REG_CL) {
        count = state_.rcx & 0x3F; // Only lower 6 bits for 64-bit mode
    } else {
        count = instr.immediate & 0x3F;
    }
    
    if (count == 0) return;
    
    uint64_t value = state_.gpr[rm];
    uint64_t result = value << count;
    
    // Update flags
    state_.rflags &= ~(ZF | SF | CF | OF | PF);
    if (result == 0) state_.rflags |= ZF;
    if (result & (1ULL << 63)) state_.rflags |= SF;
    if (count <= 64 && (value & (1ULL << (64 - count)))) state_.rflags |= CF;
    
    state_.gpr[rm] = result;
}

void CPU::handle_shr(const Instruction& instr) {
    uint8_t rm = instr.modrm & 7;
    if (instr.rex & 0x01) rm += 8;
    
    uint8_t count;
    if (instr.opcode == SHR_REG_CL) {
        count = state_.rcx & 0x3F;
    } else {
        count = instr.immediate & 0x3F;
    }
    
    if (count == 0) return;
    
    uint64_t value = state_.gpr[rm];
    uint64_t result = value >> count;
    
    // Update flags
    state_.rflags &= ~(ZF | SF | CF | OF | PF);
    if (result == 0) state_.rflags |= ZF;
    if (result & (1ULL << 63)) state_.rflags |= SF;
    if (count <= 64 && (value & (1ULL << (count - 1)))) state_.rflags |= CF;
    
    state_.gpr[rm] = result;
}

void CPU::handle_sar(const Instruction& instr) {
    uint8_t rm = instr.modrm & 7;
    if (instr.rex & 0x01) rm += 8;
    
    uint8_t count;
    if (instr.opcode == SAR_REG_CL) {
        count = state_.rcx & 0x3F;
    } else {
        count = instr.immediate & 0x3F;
    }
    
    if (count == 0) return;
    
    int64_t value = static_cast<int64_t>(state_.gpr[rm]);
    int64_t result = value >> count;
    
    // Update flags
    state_.rflags &= ~(ZF | SF | CF | OF | PF);
    if (result == 0) state_.rflags |= ZF;
    if (result < 0) state_.rflags |= SF;
    if (count <= 64 && (value & (1LL << (count - 1)))) state_.rflags |= CF;
    
    state_.gpr[rm] = static_cast<uint64_t>(result);
}

void CPU::handle_rol(const Instruction& instr) {
    uint8_t rm = instr.modrm & 7;
    if (instr.rex & 0x01) rm += 8;
    
    uint8_t count = instr.immediate & 0x3F;
    if (count == 0) return;
    
    uint64_t value = state_.gpr[rm];
    count %= 64; // Normalize count
    
    uint64_t result = (value << count) | (value >> (64 - count));
    
    // Update flags
    state_.rflags &= ~(CF | OF);
    if (result & 1) state_.rflags |= CF;
    if (count == 1 && ((result ^ value) & (1ULL << 63))) state_.rflags |= OF;
    
    state_.gpr[rm] = result;
}

void CPU::handle_ror(const Instruction& instr) {
    uint8_t rm = instr.modrm & 7;
    if (instr.rex & 0x01) rm += 8;
    
    uint8_t count = instr.immediate & 0x3F;
    if (count == 0) return;
    
    uint64_t value = state_.gpr[rm];
    count %= 64; // Normalize count
    
    uint64_t result = (value >> count) | (value << (64 - count));
    
    // Update flags
    state_.rflags &= ~(CF | OF);
    if (result & (1ULL << 63)) state_.rflags |= CF;
    if (count == 1 && ((result ^ value) & (1ULL << 63))) state_.rflags |= OF;
    
    state_.gpr[rm] = result;
}

void CPU::handle_inc(const Instruction& instr) {
    uint8_t rm = instr.modrm & 7;
    if (instr.rex & 0x01) rm += 8;
    
    uint64_t value = state_.gpr[rm];
    uint64_t result = value + 1;
    
    // Update flags (CF is not affected by INC)
    state_.rflags &= ~(ZF | SF | OF | PF | AF);
    if (result == 0) state_.rflags |= ZF;
    if (result & (1ULL << 63)) state_.rflags |= SF;
    if (value == 0x7FFFFFFFFFFFFFFFULL) state_.rflags |= OF;
    if ((value & 0xF) == 0xF) state_.rflags |= AF;
    
    state_.gpr[rm] = result;
}

void CPU::handle_dec(const Instruction& instr) {
    uint8_t rm = instr.modrm & 7;
    if (instr.rex & 0x01) rm += 8;
    
    uint64_t value = state_.gpr[rm];
    uint64_t result = value - 1;
    
    // Update flags (CF is not affected by DEC)
    state_.rflags &= ~(ZF | SF | OF | PF | AF);
    if (result == 0) state_.rflags |= ZF;
    if (result & (1ULL << 63)) state_.rflags |= SF;
    if (value == 0x8000000000000000ULL) state_.rflags |= OF;
    if ((value & 0xF) == 0) state_.rflags |= AF;
    
    state_.gpr[rm] = result;
}

void CPU::handle_div(const Instruction& instr) {
    uint8_t rm = instr.modrm & 7;
    if (instr.rex & 0x01) rm += 8;
    
    uint64_t divisor = state_.gpr[rm];
    if (divisor == 0) {
        // Division by zero exception
        handle_interrupt(0);
        return;
    }
    
    if (instr.opcode == DIV_REG) {
        // Unsigned division
        __uint128_t dividend = ((__uint128_t)state_.rdx << 64) | state_.rax;
        uint64_t quotient = dividend / divisor;
        uint64_t remainder = dividend % divisor;
        
        // Check for overflow
        if (quotient > 0xFFFFFFFFFFFFFFFFULL) {
            handle_interrupt(0);
            return;
        }
        
        state_.rax = quotient;
        state_.rdx = remainder;
    } else {
        // Signed division (IDIV)
        __int128_t dividend = ((__int128_t)static_cast<int64_t>(state_.rdx) << 64) | state_.rax;
        int64_t signed_divisor = static_cast<int64_t>(divisor);
        
        int64_t quotient = dividend / signed_divisor;
        int64_t remainder = dividend % signed_divisor;
        
        state_.rax = static_cast<uint64_t>(quotient);
        state_.rdx = static_cast<uint64_t>(remainder);
    }
}

void CPU::handle_cmov(const Instruction& instr) {
    uint8_t reg = (instr.modrm >> 3) & 7;
    uint8_t rm = instr.modrm & 7;
    
    if (instr.rex & 0x04) reg += 8;
    if (instr.rex & 0x01) rm += 8;
    
    bool condition = false;
    
    switch (instr.opcode) {
        case CMOVO:  condition = (state_.rflags & OF) != 0; break;
        case CMOVNO: condition = (state_.rflags & OF) == 0; break;
        case CMOVB:  condition = (state_.rflags & CF) != 0; break;
        case CMOVNB: condition = (state_.rflags & CF) == 0; break;
        case CMOVE:  condition = (state_.rflags & ZF) != 0; break;
        case CMOVNE: condition = (state_.rflags & ZF) == 0; break;
        case CMOVBE: condition = (state_.rflags & (CF | ZF)) != 0; break;
        case CMOVA:  condition = (state_.rflags & (CF | ZF)) == 0; break;
        case CMOVS:  condition = (state_.rflags & SF) != 0; break;
        case CMOVNS: condition = (state_.rflags & SF) == 0; break;
        case CMOVP:  condition = (state_.rflags & PF) != 0; break;
        case CMOVNP: condition = (state_.rflags & PF) == 0; break;
        case CMOVL:  condition = ((state_.rflags & SF) != 0) != ((state_.rflags & OF) != 0); break;
        case CMOVGE: condition = ((state_.rflags & SF) != 0) == ((state_.rflags & OF) != 0); break;
        case CMOVLE: condition = (state_.rflags & ZF) != 0 || (((state_.rflags & SF) != 0) != ((state_.rflags & OF) != 0)); break;
        case CMOVG:  condition = (state_.rflags & ZF) == 0 && (((state_.rflags & SF) != 0) == ((state_.rflags & OF) != 0)); break;
    }
    
    if (condition) {
        state_.gpr[reg] = state_.gpr[rm];
    }
}

void CPU::handle_string_operation(const Instruction& instr) {
    // Simplified string operations implementation
    uint64_t count = (instr.prefixes & PREFIX_REP) ? state_.rcx : 1;
    bool direction = (state_.rflags & DF) != 0;
    int64_t step = direction ? -8 : 8; // Assuming 64-bit operations
    
    for (uint64_t i = 0; i < count && running_; ++i) {
        switch (instr.opcode) {
            case MOVS:
            case MOVSW: {
                uint64_t value = read_u64_from_mem(mem_, state_.rsi);
                write_u64_to_mem(mem_, state_.rdi, value);
                state_.rsi += step;
                state_.rdi += step;
                break;
            }
            case CMPS:
            case CMPSW: {
                uint64_t src = read_u64_from_mem(mem_, state_.rsi);
                uint64_t dst = read_u64_from_mem(mem_, state_.rdi);
                uint64_t result = src - dst;
                
                // Update flags
                state_.rflags &= ~(ZF | SF | CF | OF);
                if (result == 0) state_.rflags |= ZF;
                if (result & (1ULL << 63)) state_.rflags |= SF;
                if (src < dst) state_.rflags |= CF;
                
                state_.rsi += step;
                state_.rdi += step;
                
                // Check for early termination conditions
                if (instr.prefixes & PREFIX_REPE && !(state_.rflags & ZF)) break;
                if (instr.prefixes & PREFIX_REPNE && (state_.rflags & ZF)) break;
                break;
            }
            case STOS:
            case STOSW: {
                write_u64_to_mem(mem_, state_.rdi, state_.rax);
                state_.rdi += step;
                break;
            }
            case LODS:
            case LODSW: {
                state_.rax = read_u64_from_mem(mem_, state_.rsi);
                state_.rsi += step;
                break;
            }
            case SCAS:
            case SCASW: {
                uint64_t value = read_u64_from_mem(mem_, state_.rdi);
                uint64_t result = state_.rax - value;
                
                // Update flags
                state_.rflags &= ~(ZF | SF | CF | OF);
                if (result == 0) state_.rflags |= ZF;
                if (result & (1ULL << 63)) state_.rflags |= SF;
                if (state_.rax < value) state_.rflags |= CF;
                
                state_.rdi += step;
                
                // Check for early termination conditions
                if (instr.prefixes & PREFIX_REPE && !(state_.rflags & ZF)) break;
                if (instr.prefixes & PREFIX_REPNE && (state_.rflags & ZF)) break;
                break;
            }
        }
        
        if (instr.prefixes & (PREFIX_REP | PREFIX_REPE | PREFIX_REPNE)) {
            state_.rcx--;
            if (state_.rcx == 0) break;
        }
    }
}

void CPU::handle_bit_operation(const Instruction& instr) {
    uint8_t reg = (instr.modrm >> 3) & 7;
    uint8_t rm = instr.modrm & 7;
    
    if (instr.rex & 0x04) reg += 8;
    if (instr.rex & 0x01) rm += 8;
    
    switch (instr.opcode) {
        case BSF: {
            uint64_t value = state_.gpr[rm];
            if (value == 0) {
                state_.rflags |= ZF;
            } else {
                state_.rflags &= ~ZF;
                for (int i = 0; i < 64; ++i) {
                    if (value & (1ULL << i)) {
                        state_.gpr[reg] = i;
                        break;
                    }
                }
            }
            break;
        }
        case BSR: {
            uint64_t value = state_.gpr[rm];
            if (value == 0) {
                state_.rflags |= ZF;
            } else {
                state_.rflags &= ~ZF;
                for (int i = 63; i >= 0; --i) {
                    if (value & (1ULL << i)) {
                        state_.gpr[reg] = i;
                        break;
                    }
                }
            }
            break;
        }
        case BT: {
            uint64_t bit_pos = state_.gpr[reg] & 63;
            bool bit_value = (state_.gpr[rm] & (1ULL << bit_pos)) != 0;
            if (bit_value) state_.rflags |= CF;
            else state_.rflags &= ~CF;
            break;
        }
        case BTC: {
            uint64_t bit_pos = state_.gpr[reg] & 63;
            bool bit_value = (state_.gpr[rm] & (1ULL << bit_pos)) != 0;
            if (bit_value) state_.rflags |= CF;
            else state_.rflags &= ~CF;
            state_.gpr[rm] ^= (1ULL << bit_pos);
            break;
        }
        case BTR: {
            uint64_t bit_pos = state_.gpr[reg] & 63;
            bool bit_value = (state_.gpr[rm] & (1ULL << bit_pos)) != 0;
            if (bit_value) state_.rflags |= CF;
            else state_.rflags &= ~CF;
            state_.gpr[rm] &= ~(1ULL << bit_pos);
            break;
        }
        case BTS: {
            uint64_t bit_pos = state_.gpr[reg] & 63;
            bool bit_value = (state_.gpr[rm] & (1ULL << bit_pos)) != 0;
            if (bit_value) state_.rflags |= CF;
            else state_.rflags &= ~CF;
            state_.gpr[rm] |= (1ULL << bit_pos);
            break;
        }
    }
}

void CPU::handle_lea(const Instruction& instr) {
    uint8_t reg = (instr.modrm >> 3) & 7;
    if (instr.rex & 0x04) reg += 8;
    
    uint64_t effective_addr = calculate_effective_address(instr);
    state_.gpr[reg] = effective_addr;
}

void CPU::handle_xchg(const Instruction& instr) {
    uint8_t reg = (instr.modrm >> 3) & 7;
    uint8_t rm = instr.modrm & 7;
    
    if (instr.rex & 0x04) reg += 8;
    if (instr.rex & 0x01) rm += 8;
    
    uint64_t temp = state_.gpr[reg];
    state_.gpr[reg] = state_.gpr[rm];
    state_.gpr[rm] = temp;
}

void CPU::handle_xadd(const Instruction& instr) {
    uint8_t reg = (instr.modrm >> 3) & 7;
    uint8_t rm = instr.modrm & 7;
    
    if (instr.rex & 0x04) reg += 8;
    if (instr.rex & 0x01) rm += 8;
    
    uint64_t src = state_.gpr[reg];
    uint64_t dst = state_.gpr[rm];
    uint64_t result = src + dst;
    
    // Update flags like ADD
    state_.rflags &= ~(ZF | SF | CF | OF | PF | AF);
    if (result == 0) state_.rflags |= ZF;
    if (result & (1ULL << 63)) state_.rflags |= SF;
    if (result < dst) state_.rflags |= CF;
    
    state_.gpr[reg] = dst;  // Original destination goes to source
    state_.gpr[rm] = result; // Sum goes to destination
}

void CPU::handle_cmpxchg(const Instruction& instr) {
    uint8_t reg = (instr.modrm >> 3) & 7;
    uint8_t rm = instr.modrm & 7;
    
    if (instr.rex & 0x04) reg += 8;
    if (instr.rex & 0x01) rm += 8;
    
    if (instr.opcode == CMPXCHG) {
        uint64_t accumulator = state_.rax;
        uint64_t destination = state_.gpr[rm];
        uint64_t source = state_.gpr[reg];
        
        if (accumulator == destination) {
            state_.gpr[rm] = source;
            state_.rflags |= ZF;
        } else {
            state_.rax = destination;
            state_.rflags &= ~ZF;
        }
    } else if (instr.opcode == CMPXCHG8B) {
        // 8-byte compare and exchange
        uint64_t addr = calculate_effective_address(instr);
        uint64_t edx_eax = (state_.rdx << 32) | (state_.rax & 0xFFFFFFFF);
        uint64_t memory_value = read_u64_from_mem(mem_, addr);
        
        if (edx_eax == memory_value) {
            uint64_t ecx_ebx = (state_.rcx << 32) | (state_.rbx & 0xFFFFFFFF);
            write_u64_to_mem(mem_, addr, ecx_ebx);
            state_.rflags |= ZF;
        } else {
            state_.rax = memory_value & 0xFFFFFFFF;
            state_.rdx = memory_value >> 32;
            state_.rflags &= ~ZF;
        }
    }
}

void CPU::handle_setcc(const Instruction& instr) {
    uint8_t rm = instr.modrm & 7;
    if (instr.rex & 0x01) rm += 8;
    
    bool condition = false;
    
    switch (instr.opcode) {
        case SETO:  condition = (state_.rflags & OF) != 0; break;
        case SETNO: condition = (state_.rflags & OF) == 0; break;
        case SETB:  condition = (state_.rflags & CF) != 0; break;
        case SETNB: condition = (state_.rflags & CF) == 0; break;
        case SETE:  condition = (state_.rflags & ZF) != 0; break;
        case SETNE: condition = (state_.rflags & ZF) == 0; break;
        case SETBE: condition = (state_.rflags & (CF | ZF)) != 0; break;
        case SETA:  condition = (state_.rflags & (CF | ZF)) == 0; break;
        case SETS:  condition = (state_.rflags & SF) != 0; break;
        case SETNS: condition = (state_.rflags & SF) == 0; break;
        case SETP:  condition = (state_.rflags & PF) != 0; break;
        case SETNP: condition = (state_.rflags & PF) == 0; break;
        case SETL:  condition = ((state_.rflags & SF) != 0) != ((state_.rflags & OF) != 0); break;
        case SETGE: condition = ((state_.rflags & SF) != 0) == ((state_.rflags & OF) != 0); break;
        case SETLE: condition = (state_.rflags & ZF) != 0 || (((state_.rflags & SF) != 0) != ((state_.rflags & OF) != 0)); break;
        case SETG:  condition = (state_.rflags & ZF) == 0 && (((state_.rflags & SF) != 0) == ((state_.rflags & OF) != 0)); break;
    }
    
    state_.gpr[rm] = (state_.gpr[rm] & 0xFFFFFFFFFFFFFF00ULL) | (condition ? 1 : 0);
}

void CPU::handle_loop(const Instruction& instr) {
    state_.rcx--;
    
    bool should_loop = false;
    
    switch (instr.opcode) {
        case LOOP:
            should_loop = (state_.rcx != 0);
            break;
        case LOOPE:
            should_loop = (state_.rcx != 0) && (state_.rflags & ZF);
            break;
        case LOOPNE:
            should_loop = (state_.rcx != 0) && !(state_.rflags & ZF);
            break;
        case JCXZ:
            should_loop = (state_.rcx == 0);
            break;
    }
    
    if (should_loop) {
        int8_t offset = static_cast<int8_t>(instr.immediate);
        state_.rip += offset;
    }
}

void CPU::handle_flag_operation(const Instruction& instr) {
    switch (instr.opcode) {
        case CLC: state_.rflags &= ~CF; break;
        case STC: state_.rflags |= CF; break;
        case CLI: state_.rflags &= ~IF; interrupts_enabled_ = false; break;
        case STI: state_.rflags |= IF; interrupts_enabled_ = true; break;
        case CLD: state_.rflags &= ~DF; break;
        case STD: state_.rflags |= DF; break;
        case CMC: state_.rflags ^= CF; break;
    }
}

void CPU::handle_pushf(const Instruction& instr) {
    state_.rsp -= 8;
    write_u64_to_mem(mem_, state_.rsp, state_.rflags);
}

void CPU::handle_popf(const Instruction& instr) {
    state_.rflags = read_u64_from_mem(mem_, state_.rsp);
    state_.rsp += 8;
    interrupts_enabled_ = (state_.rflags & IF) != 0;
}

void CPU::handle_sysret(const Instruction& instr) {
    // SYSRET implementation (simplified)
    state_.rip = state_.rcx;
    state_.rflags = state_.r11;
    state_.cs = 0x23; // User code segment
    state_.cpl = 3;   // User mode
}

void CPU::handle_interrupt_instruction(const Instruction& instr) {
    uint8_t vector;
    if (instr.opcode == INT3) {
        vector = 3;
    } else if (instr.opcode == INTO) {
        if (!(state_.rflags & OF)) return;
        vector = 4;
    } else {
        vector = instr.immediate & 0xFF;
    }
    
    handle_interrupt(vector);
}

void CPU::handle_iret(const Instruction& instr) {
    // Pop RIP, CS, and RFLAGS
    state_.rip = read_u64_from_mem(mem_, state_.rsp);
    state_.rsp += 8;
    state_.cs = read_u64_from_mem(mem_, state_.rsp) & 0xFFFF;
    state_.rsp += 8;
    state_.rflags = read_u64_from_mem(mem_, state_.rsp);
    state_.rsp += 8;
    
    interrupts_enabled_ = (state_.rflags & IF) != 0;
}

void CPU::handle_cpuid(const Instruction& instr) {
    uint32_t leaf = state_.rax & 0xFFFFFFFF;
    uint32_t subleaf = state_.rcx & 0xFFFFFFFF;
    
    // Simplified CPUID implementation for PS5 emulation
    // TODO: Implement CPUID
    switch (leaf) {
        case 0: // Basic CPUID information
            state_.rax = 0x16; // Maximum input value
            state_.rbx = 0x756E6547; // "Genu"
            state_.rdx = 0x49656E69; // "ineI"
            state_.rcx = 0x6C65746E; // "ntel"
            break;
        case 1: // Processor info and feature bits
            state_.rax = 0x000906E9; // Family, model, stepping
            state_.rbx = 0x00100800; // Brand index, CLFLUSH size, etc.
            state_.rcx = 0x7FFAFBBF; // Feature flags (ECX)
            state_.rdx = 0xBFEBFBFF; // Feature flags (EDX)
            break;
        case 7: // Extended features
            if (subleaf == 0) {
                state_.rax = 0;
                state_.rbx = 0x029C67AF; // Extended feature flags
                state_.rcx = 0x00000000;
                state_.rdx = 0x00000000;
            }
            break;
        default:
            state_.rax = state_.rbx = state_.rcx = state_.rdx = 0;
            break;
    }
}

void CPU::handle_rdtsc(const Instruction& instr) {
    // Simplified TSC implementation
    // TODO: Implement accurate TSC emulation
    static uint64_t tsc_counter = 0;
    tsc_counter += 1000; // Increment by arbitrary amount
    
    state_.rax = tsc_counter & 0xFFFFFFFF;
    state_.rdx = tsc_counter >> 32;
    
    if (instr.opcode == RDTSCP) {
        state_.rcx = 0; // Processor ID
    }
}

void CPU::handle_cache_operation(const Instruction& instr) {
    // Cache operations are mostly no-ops in emulation
    switch (instr.opcode) {
        case PREFETCH:
            // Prefetch hint - no-op in emulation
            break;
        case CLFLUSH:
            // Cache line flush - no-op in emulation
            break;
    }
}

void CPU::handle_memory_barrier(const Instruction& instr) {
    // Memory barriers - ensure ordering in emulation
    switch (instr.opcode) {
        case MFENCE:
            std::atomic_thread_fence(std::memory_order_seq_cst);
            break;
        case SFENCE:
            // Store fence
            std::atomic_thread_fence(std::memory_order_release);
            break;
        case LFENCE:
            // Load fence
            std::atomic_thread_fence(std::memory_order_acquire);
            break;
    }
}
