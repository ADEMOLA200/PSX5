#include "core/jit_dynarec.h"
#include "core/cpu.h"
#include <iostream>
#include <algorithm>
#include <fstream>
#include <chrono>

#ifdef PSX5_ENABLE_ASMJIT
#include <asmjit/asmjit.h>
#endif

DynarecBase::DynarecBase() 
    : optimization_level_(OptimizationLevel::BASIC)
    , profiling_enabled_(true)
    , max_trace_length_(64)
    , hot_threshold_(100)
{
    memset(&stats_, 0, sizeof(stats_));
    std::cout << "DynarecBase: Enhanced dynamic recompiler initialized" << std::endl;
}

DynarecBase::~DynarecBase() {
    std::cout << "DynarecBase: Statistics - Traces compiled: " << stats_.traces_compiled
              << ", Executed: " << stats_.traces_executed
              << ", Cache hit rate: " << (stats_.cache_hits * 100.0f / (stats_.cache_hits + stats_.cache_misses)) << "%"
              << std::endl;
}

std::optional<TraceCompiled> DynarecBase::compile_trace(const uint8_t* mem, size_t pc, size_t max_bytes) {
    return compile_optimized_trace(mem, pc, max_bytes, optimization_level_);
}

std::optional<TraceCompiled> DynarecBase::compile_x86_trace(const uint8_t* mem, size_t pc, size_t max_bytes) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = trace_cache_.find(pc);
        if (it != trace_cache_.end()) {
            stats_.cache_hits++;
            it->second.execution_count++;
            it->second.last_executed = std::chrono::high_resolution_clock::now();
            return it->second;
        }
        stats_.cache_misses++;
    }
    
    // Decode x86-64 instruction sequence
    std::vector<X86Instruction> instructions = decode_x86_sequence(mem, pc, max_bytes);
    if (instructions.empty()) {
        return std::nullopt;
    }
    
    // Apply optimizations based on level
    if (optimization_level_ >= OptimizationLevel::BASIC) {
        apply_peephole_optimizations(instructions);
        eliminate_redundant_moves(instructions);
    }
    
    if (optimization_level_ >= OptimizationLevel::AGGRESSIVE) {
        optimize_memory_accesses(instructions);
        optimize_branch_sequences(instructions);
    }
    
    if (optimization_level_ >= OptimizationLevel::MAXIMUM) {
        optimize_instruction_sequence(instructions);
    }
    
    // Calculate bytes consumed
    size_t bytes_consumed = 0;
    for (const auto& instr : instructions) {
        bytes_consumed += instr.length;
    }
    
    // Generate optimized code
    std::optional<TraceCompiled> result;
    
#ifdef PSX5_ENABLE_ASMJIT
    result = generate_asmjit_trace(instructions);
#endif
    
    if (!result) {
        result = generate_interpreter_trace(instructions, pc, bytes_consumed);
    }
    
    if (result) {
        result->bytes_consumed = bytes_consumed;
        result->execution_count = 0;
        result->total_cycles = 0;
        result->is_hot_trace = false;
        result->optimization_level = static_cast<uint32_t>(optimization_level_);
        
        // Extract branch targets for profiling
        for (const auto& instr : instructions) {
            if (instr.is_branch && instr.displacement != 0) {
                uint64_t target = instr.pc + instr.length + instr.displacement;
                result->branch_targets.push_back(target);
            }
        }
        
        // Cache the result
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            trace_cache_[pc] = *result;
        }
        
        stats_.traces_compiled++;
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        stats_.total_compilation_time_us += duration.count();
        
        std::cout << "DynarecBase: Compiled trace at 0x" << std::hex << pc << std::dec
                  << " (" << instructions.size() << " instructions, " 
                  << duration.count() << "μs)" << std::endl;
    }
    
    return result;
}

std::optional<TraceCompiled> DynarecBase::compile_optimized_trace(const uint8_t* mem, size_t pc, 
                                                                 size_t max_bytes, OptimizationLevel level) {
    OptimizationLevel old_level = optimization_level_;
    optimization_level_ = level;
    
    auto result = compile_x86_trace(mem, pc, max_bytes);
    
    optimization_level_ = old_level;
    return result;
}

std::vector<DynarecBase::X86Instruction> DynarecBase::decode_x86_sequence(const uint8_t* mem, size_t pc, size_t max_bytes) {
    std::vector<X86Instruction> instructions;
    size_t current_pc = pc;
    size_t end_pc = pc + max_bytes;
    
    while (current_pc < end_pc && instructions.size() < max_trace_length_) {
        X86Instruction instr = {};
        instr.pc = current_pc;
        
        const uint8_t* code = &mem[current_pc];
        size_t offset = 0;
        
        // Parse prefixes
        while (offset < 15 && current_pc + offset < end_pc) {
            uint8_t byte = code[offset];
            if (byte == 0xF0 || byte == 0xF2 || byte == 0xF3 || // LOCK, REPNE, REP
                byte == 0x2E || byte == 0x36 || byte == 0x3E || byte == 0x26 || // Segment overrides
                byte == 0x64 || byte == 0x65 || // FS/GS
                byte == 0x66 || byte == 0x67) { // Operand/Address size
                instr.prefixes |= (1 << (byte & 0xF));
                offset++;
            } else {
                break;
            }
        }
        
        // Parse REX prefix
        if (offset < 15 && current_pc + offset < end_pc && 
            code[offset] >= 0x40 && code[offset] <= 0x4F) {
            instr.rex = code[offset];
            offset++;
        }
        
        // Parse opcode
        if (current_pc + offset >= end_pc) break;
        instr.opcode = code[offset++];
        
        // Handle two-byte opcodes
        if (instr.opcode == 0x0F) {
            if (current_pc + offset >= end_pc) break;
            instr.opcode = (instr.opcode << 8) | code[offset++];
            
            // Handle three-byte opcodes
            if ((code[offset-1] == 0x38 || code[offset-1] == 0x3A) && current_pc + offset < end_pc) {
                instr.opcode = (instr.opcode << 8) | code[offset++];
            }
        }
        
        // Parse ModR/M and SIB if present
        // TODO: implement ModR/M and SIB parsing
        bool has_modrm = true; // Simplified - most instructions have ModR/M
        if (has_modrm && current_pc + offset < end_pc) {
            instr.modrm = code[offset++];
            uint8_t mod = (instr.modrm >> 6) & 3;
            uint8_t rm = instr.modrm & 7;
            
            // SIB byte
            if (mod != 3 && rm == 4 && current_pc + offset < end_pc) {
                instr.sib = code[offset++];
            }
            
            // Displacement
            if (mod == 1 && current_pc + offset < end_pc) {
                instr.displacement = static_cast<int8_t>(code[offset++]);
            } else if (mod == 2 || (mod == 0 && rm == 5)) {
                if (current_pc + offset + 4 <= end_pc) {
                    instr.displacement = *reinterpret_cast<const int32_t*>(&code[offset]);
                    offset += 4;
                }
            }
        }
        
        // Parse immediate operand (simplified)
        size_t imm_size = 0;
        switch (instr.opcode) {
            case 0x81: imm_size = 4; break; // 32-bit immediate
            case 0xE8: case 0xE9: imm_size = 4; break; // 32-bit relative
            case 0xEB: imm_size = 1; break; // 8-bit relative
            case 0xB8: case 0xB9: case 0xBA: case 0xBB: // MOV reg, imm
            case 0xBC: case 0xBD: case 0xBE: case 0xBF:
                imm_size = (instr.rex & 0x08) ? 8 : 4;
                break;
        }
        
        if (imm_size > 0 && current_pc + offset + imm_size <= end_pc) {
            switch (imm_size) {
                case 1: instr.immediate = static_cast<int8_t>(code[offset]); break;
                case 4: instr.immediate = *reinterpret_cast<const int32_t*>(&code[offset]); break;
                case 8: instr.immediate = *reinterpret_cast<const uint64_t*>(&code[offset]); break;
            }
            offset += imm_size;
        }
        
        instr.length = offset;
        
        // Analyze instruction properties
        switch (instr.opcode) {
            case 0x89: case 0x8B: // MOV
                instr.reads_memory = (instr.opcode == 0x8B);
                instr.writes_memory = (instr.opcode == 0x89);
                break;
            case 0xE8: // CALL
                instr.is_call = true;
                instr.is_branch = true;
                break;
            case 0xE9: case 0xEB: // JMP
                instr.is_branch = true;
                break;
            case 0x0F84: case 0x0F85: case 0x0F8C: case 0x0F8F: // Conditional jumps
                instr.is_branch = true;
                instr.is_conditional = true;
                break;
        }
        
        instructions.push_back(instr);
        current_pc += instr.length;
        
        // Stop at block terminators
        if (is_block_terminator(instr.opcode)) {
            break;
        }
    }
    
    return instructions;
}

void DynarecBase::apply_peephole_optimizations(std::vector<X86Instruction>& instructions) {
    for (size_t i = 0; i < instructions.size() - 1; ++i) {
        auto& instr1 = instructions[i];
        auto& instr2 = instructions[i + 1];
        
        // Pattern: MOV reg, imm followed by ADD reg, imm -> MOV reg, (imm1 + imm2)
        if (instr1.opcode >= 0xB8 && instr1.opcode <= 0xBF && // MOV reg, imm
            instr2.opcode == 0x81 && (instr2.modrm & 0x38) == 0x00) { // ADD reg, imm
            
            uint8_t reg1 = instr1.opcode - 0xB8;
            uint8_t reg2 = instr2.modrm & 7;
            
            if (reg1 == reg2) {
                // Combine the immediates
                instr1.immediate += instr2.immediate;
                // Mark second instruction for removal
                instr2.opcode = 0x90; // NOP
            }
        }
        
        // Pattern: XOR reg, reg -> MOV reg, 0 (faster on some CPUs)
        if (instr1.opcode == 0x31) { // XOR reg, reg
            uint8_t reg1 = (instr1.modrm >> 3) & 7;
            uint8_t reg2 = instr1.modrm & 7;
            if (reg1 == reg2) {
                instr1.opcode = 0xB8 + reg1; // MOV reg, imm
                instr1.immediate = 0;
                instr1.modrm = 0;
            }
        }
    }
    
    // Remove NOPs
    instructions.erase(
        std::remove_if(instructions.begin(), instructions.end(),
            [](const X86Instruction& instr) { return instr.opcode == 0x90; }),
        instructions.end()
    );
}

void DynarecBase::eliminate_redundant_moves(std::vector<X86Instruction>& instructions) {
    for (size_t i = 0; i < instructions.size() - 1; ++i) {
        auto& instr1 = instructions[i];
        auto& instr2 = instructions[i + 1];
        
        // Pattern: MOV A, B followed by MOV B, A -> eliminate second move
        if (instr1.opcode == 0x89 && instr2.opcode == 0x89) { // Both MOV r/m, r
            uint8_t src1 = (instr1.modrm >> 3) & 7;
            uint8_t dst1 = instr1.modrm & 7;
            uint8_t src2 = (instr2.modrm >> 3) & 7;
            uint8_t dst2 = instr2.modrm & 7;
            
            if (src1 == dst2 && dst1 == src2) {
                instr2.opcode = 0x90; // NOP
            }
        }
    }
}

void DynarecBase::optimize_memory_accesses(std::vector<X86Instruction>& instructions) {
    // Look for repeated memory accesses to the same location
    std::unordered_map<int32_t, size_t> last_load;
    
    for (size_t i = 0; i < instructions.size(); ++i) {
        auto& instr = instructions[i];
        
        if (instr.reads_memory && !instr.writes_memory) {
            auto it = last_load.find(instr.displacement);
            if (it != last_load.end() && i - it->second < 8) {
                // Recent load from same address - could potentially optimize
                // This is a placeholder for more sophisticated memory optimization
                // TODO: implement memory access optimization
            }
            last_load[instr.displacement] = i;
        }
        
        if (instr.writes_memory) {
            last_load.clear();
        }
    }
}

void DynarecBase::optimize_branch_sequences(std::vector<X86Instruction>& instructions) {
    for (size_t i = 0; i < instructions.size() - 2; ++i) {
        auto& instr1 = instructions[i];
        auto& instr2 = instructions[i + 1];
        auto& instr3 = instructions[i + 2];
        
        // Pattern: CMP followed by conditional jump followed by unconditional jump
        // This can be optimized to a single conditional jump
        if (instr1.opcode == 0x39 && // CMP
            instr2.is_conditional && instr2.is_branch &&
            instr3.opcode == 0xE9) { // Unconditional JMP
            
            // Could potentially combine these into a more efficient sequence
            // This is a placeholder for more sophisticated branch optimization
            // TODO: implement branch optimization
        }
    }
}

void DynarecBase::optimize_instruction_sequence(std::vector<X86Instruction>& instructions) {
    // Advanced optimizations for maximum performance
    
    // Strength reduction: replace expensive operations with cheaper ones
    for (auto& instr : instructions) {
        // MUL by power of 2 -> SHL
        if (instr.opcode == 0xF7 && (instr.modrm & 0x38) == 0x20) { // MUL
            if (instr.immediate > 0 && (instr.immediate & (instr.immediate - 1)) == 0) {
                // Power of 2 - convert to shift
                uint8_t shift_count = 0;
                uint64_t temp = instr.immediate;
                while (temp > 1) {
                    temp >>= 1;
                    shift_count++;
                }
                instr.opcode = 0xC1; // SHL
                instr.modrm = (instr.modrm & 0xC7) | 0x20;
                instr.immediate = shift_count;
            }
        }
    }
    
    // Instruction scheduling to reduce pipeline stalls
    // This is a simplified version - real implementation would be much more complex (TODO)
    // TODO: implement much more complex scheduling based on instruction latencies and dependencies
    for (size_t i = 0; i < instructions.size() - 1; ++i) {
        auto& current = instructions[i];
        auto& next = instructions[i + 1];
        
        // If current instruction has high latency and next doesn't depend on it,
        // try to find an independent instruction to insert
        if (estimate_instruction_cycles(current) > 2 && !next.is_branch) {
            // Look ahead for independent instructions
            for (size_t j = i + 2; j < std::min(i + 5, instructions.size()); ++j) {
                auto& candidate = instructions[j];
                if (!candidate.is_branch && !candidate.is_call) {
                    // Simple dependency check - real implementation would be more thorough
                    bool independent = true;
                    // TODO: implement dependency analysis

                    if (independent) {
                        // Swap instructions to reduce stall
                        std::swap(instructions[i + 1], instructions[j]);
                        break;
                    }
                }
            }
        }
    }
}

#ifdef PSX5_ENABLE_ASMJIT
std::optional<TraceCompiled> DynarecBase::generate_asmjit_trace(const std::vector<X86Instruction>& instructions) {
    try {
        static asmjit::JitRuntime rt;
        asmjit::CodeHolder code;
        code.init(rt.environment());
        asmjit::x86::Assembler a(&code);
        
        // Function signature: void fn(CPU& cpu)
        // CPU* is in RDI (first argument)
        
        // Generate prologue
        a.push(asmjit::x86::rbp);
        a.mov(asmjit::x86::rbp, asmjit::x86::rsp);
        a.push(asmjit::x86::rbx);
        a.push(asmjit::x86::r12);
        a.push(asmjit::x86::r13);
        a.push(asmjit::x86::r14);
        a.push(asmjit::x86::r15);
        
        // Load CPU register base into R12
        a.mov(asmjit::x86::r12, asmjit::x86::rdi); // CPU* -> R12
        
        size_t total_bytes = 0;
        for (const auto& instr : instructions) {
            total_bytes += instr.length;
            
            // Generate optimized code for each instruction
            switch (instr.opcode) {
                case 0x89: // MOV r/m, r
                    if ((instr.modrm >> 6) == 3) { // Register to register
                        uint8_t src = (instr.modrm >> 3) & 7;
                        uint8_t dst = instr.modrm & 7;
                        // mov [r12 + dst*8], [r12 + src*8] (simplified)
                        a.mov(asmjit::x86::rax, asmjit::x86::qword_ptr(asmjit::x86::r12, src * 8));
                        a.mov(asmjit::x86::qword_ptr(asmjit::x86::r12, dst * 8), asmjit::x86::rax);
                    }
                    break;
                    
                case 0x8B: // MOV r, r/m
                    if ((instr.modrm >> 6) == 3) { // Register to register
                        uint8_t dst = (instr.modrm >> 3) & 7;
                        uint8_t src = instr.modrm & 7;
                        a.mov(asmjit::x86::rax, asmjit::x86::qword_ptr(asmjit::x86::r12, src * 8));
                        a.mov(asmjit::x86::qword_ptr(asmjit::x86::r12, dst * 8), asmjit::x86::rax);
                    }
                    break;
                    
                case 0x01: // ADD r/m, r
                    if ((instr.modrm >> 6) == 3) {
                        uint8_t src = (instr.modrm >> 3) & 7;
                        uint8_t dst = instr.modrm & 7;
                        a.mov(asmjit::x86::rax, asmjit::x86::qword_ptr(asmjit::x86::r12, dst * 8));
                        a.add(asmjit::x86::rax, asmjit::x86::qword_ptr(asmjit::x86::r12, src * 8));
                        a.mov(asmjit::x86::qword_ptr(asmjit::x86::r12, dst * 8), asmjit::x86::rax);
                    }
                    break;
                    
                case 0x81: // Immediate arithmetic
                    {
                        uint8_t reg = instr.modrm & 7;
                        uint8_t operation = (instr.modrm >> 3) & 7;
                        a.mov(asmjit::x86::rax, asmjit::x86::qword_ptr(asmjit::x86::r12, reg * 8));
                        
                        switch (operation) {
                            case 0: // ADD
                                a.add(asmjit::x86::rax, asmjit::imm(instr.immediate));
                                break;
                            case 5: // SUB
                                a.sub(asmjit::x86::rax, asmjit::imm(instr.immediate));
                                break;
                            case 4: // AND
                                a.and_(asmjit::x86::rax, asmjit::imm(instr.immediate));
                                break;
                            case 1: // OR
                                a.or_(asmjit::x86::rax, asmjit::imm(instr.immediate));
                                break;
                            case 6: // XOR
                                a.xor_(asmjit::x86::rax, asmjit::imm(instr.immediate));
                                break;
                        }
                        
                        a.mov(asmjit::x86::qword_ptr(asmjit::x86::r12, reg * 8), asmjit::x86::rax);
                    }
                    break;
                    
                default:
                    // For unsupported instructions, fall back to interpreter
                    break;
            }
        }
        
        // Update PC
        a.add(asmjit::x86::qword_ptr(asmjit::x86::r12, offsetof(CPU, pc_)), asmjit::imm(total_bytes));
        
        // Generate epilogue
        a.pop(asmjit::x86::r15);
        a.pop(asmjit::x86::r14);
        a.pop(asmjit::x86::r13);
        a.pop(asmjit::x86::r12);
        a.pop(asmjit::x86::rbx);
        a.pop(asmjit::x86::rbp);
        a.ret();
        
        using Fn = void(*)(CPU&);
        Fn fnptr;
        asmjit::Error err = rt.add(&fnptr, &code);
        if (err) {
            std::cerr << "DynarecBase: AsmJit compilation failed: " << err << std::endl;
            return std::nullopt;
        }
        
        TraceCompiled tc;
        tc.fn = [fnptr](CPU& cpu) { fnptr(cpu); };
        return tc;
        
    } catch (const std::exception& e) {
        std::cerr << "DynarecBase: AsmJit exception: " << e.what() << std::endl;
        return std::nullopt;
    }
}
#endif

std::optional<TraceCompiled> DynarecBase::generate_interpreter_trace(const std::vector<X86Instruction>& instructions, 
                                                                    uint64_t start_pc, size_t bytes_consumed) {
    TraceCompiled tc;
    tc.fn = [instructions, bytes_consumed](CPU& cpu) {
        // Execute each instruction in the trace
        for (const auto& instr : instructions) {
            // Simplified interpreter execution
            // TODO: implement proper interpreter execution
            switch (instr.opcode) {
                case 0x89: // MOV r/m, r
                    if ((instr.modrm >> 6) == 3) {
                        uint8_t src = (instr.modrm >> 3) & 7;
                        uint8_t dst = instr.modrm & 7;
                        cpu.regs()[dst] = cpu.regs()[src];
                    }
                    break;
                    
                case 0x01: // ADD r/m, r
                    if ((instr.modrm >> 6) == 3) {
                        uint8_t src = (instr.modrm >> 3) & 7;
                        uint8_t dst = instr.modrm & 7;
                        cpu.regs()[dst] += cpu.regs()[src];
                    }
                    break;
                    
                case 0x81: // Immediate arithmetic
                    {
                        uint8_t reg = instr.modrm & 7;
                        uint8_t operation = (instr.modrm >> 3) & 7;
                        
                        switch (operation) {
                            case 0: cpu.regs()[reg] += instr.immediate; break; // ADD
                            case 5: cpu.regs()[reg] -= instr.immediate; break; // SUB
                            case 4: cpu.regs()[reg] &= instr.immediate; break; // AND
                            case 1: cpu.regs()[reg] |= instr.immediate; break; // OR
                            case 6: cpu.regs()[reg] ^= instr.immediate; break; // XOR
                        }
                    }
                    break;
                    
                default:
                    // Unsupported instruction - could call into full CPU emulator
                    break;
            }
        }
        
        cpu.pc_ += bytes_consumed;
    };
    
    return tc;
}

bool DynarecBase::is_block_terminator(uint16_t opcode) const {
    switch (opcode) {
        case 0xC3: case 0xCB: // RET, RETF
        case 0xE9: case 0xEB: // JMP rel32, JMP rel8
        case 0x0F84: case 0x0F85: case 0x0F8C: case 0x0F8F: // Conditional jumps
        case 0xE8: // CALL (ends basic block)
            return true;
        default:
            return false;
    }
}

bool DynarecBase::is_branch_instruction(uint16_t opcode) const {
    return (opcode >= 0x0F80 && opcode <= 0x0F8F) || // Conditional jumps
           opcode == 0xE9 || opcode == 0xEB ||        // Unconditional jumps
           opcode == 0xE8;                            // CALL
}

uint64_t DynarecBase::estimate_instruction_cycles(const X86Instruction& instr) const {
    if (instr.reads_memory && instr.writes_memory) return 4;
    if (instr.reads_memory || instr.writes_memory) return 3;
    if (instr.is_branch) return instr.is_conditional ? 2 : 1;
    if (instr.opcode == 0xF7) return 8; // MUL/DIV
    return 1;
}

size_t DynarecBase::calculate_trace_complexity(const std::vector<X86Instruction>& instructions) const {
    size_t complexity = 0;
    for (const auto& instr : instructions) {
        complexity += estimate_instruction_cycles(instr);
        if (instr.is_branch) complexity += 2; // Branches add complexity
        if (instr.reads_memory || instr.writes_memory) complexity += 1;
    }
    return complexity;
}

void DynarecBase::invalidate_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    trace_cache_.clear();
    trace_profiles_.clear();
    std::cout << "DynarecBase: Trace cache invalidated" << std::endl;
}

void DynarecBase::invalidate_range(uint64_t start, uint64_t end) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = trace_cache_.begin();
    while (it != trace_cache_.end()) {
        if (it->first >= start && it->first < end) {
            it = trace_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<uint64_t> DynarecBase::get_hot_traces(float threshold) const {
    std::vector<uint64_t> hot_traces;
    
    uint32_t max_executions = 0;
    for (const auto& [pc, profile] : trace_profiles_) {
        max_executions = std::max(max_executions, profile.execution_count);
    }
    
    uint32_t threshold_count = static_cast<uint32_t>(max_executions * threshold);
    for (const auto& [pc, profile] : trace_profiles_) {
        if (profile.execution_count >= threshold_count) {
            hot_traces.push_back(pc);
        }
    }
    
    return hot_traces;
}

void DynarecBase::dump_performance_report(const std::string& filename) const {
    std::ofstream report(filename);
    if (!report.is_open()) {
        std::cerr << "DynarecBase: Failed to open performance report: " << filename << std::endl;
        return;
    }
    
    report << "PS5 Emulator DynarecBase Performance Report\n";
    report << "==========================================\n\n";
    
    report << "Compilation Statistics:\n";
    report << "  Traces Compiled: " << stats_.traces_compiled << "\n";
    report << "  Traces Executed: " << stats_.traces_executed << "\n";
    report << "  Cache Hit Rate: " << (stats_.cache_hits * 100.0f / (stats_.cache_hits + stats_.cache_misses)) << "%\n";
    report << "  Average Trace Length: " << stats_.average_trace_length << "\n";
    report << "  Compilation Overhead: " << (stats_.compilation_overhead_ratio * 100.0f) << "%\n\n";
    
    report << "Hot Traces (>100 executions):\n";
    for (const auto& [pc, profile] : trace_profiles_) {
        if (profile.execution_count > 100) {
            report << "  0x" << std::hex << pc << std::dec 
                   << " - " << profile.execution_count << " executions, "
                   << "IPC: " << profile.average_ipc << "\n";
        }
    }
    
    report.close();
    std::cout << "DynarecBase: Performance report written to " << filename << std::endl;
}
