#include "core/jit.h"
#include "core/jit_asmjit.h"
#include "core/memory.h"
#include "core/cpu.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <unordered_set>
#include <fstream>
#include <mutex>

#ifdef PSX5_ENABLE_LLVM
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Constants.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Analysis/Passes.h>
#endif

JIT::JIT() : backend_(Backend::ASMJIT_X64), optimization_level_(2), profiling_enabled_(true) {
    memset(&stats, 0, sizeof(stats));
    
    trace_level_ = TraceLevel::BASIC;
    instruction_tracing_enabled_ = false;
    performance_counters_enabled_ = true;
    memset(&performance_counters_, 0, sizeof(performance_counters_));
    start_time_ = std::chrono::high_resolution_clock::now();
    
#ifdef PSX5_ENABLE_LLVM
    // Initialize LLVM
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    
    llvm_context_ = std::make_unique<llvm::LLVMContext>();
    llvm_module_ = std::make_unique<llvm::Module>("PS5Emulator", *llvm_context_);
    
    // Create execution engine
    std::string error_str;
    llvm_engine_ = llvm::EngineBuilder(std::unique_ptr<llvm::Module>(llvm_module_.get()))
        .setErrorStr(&error_str)
        .setEngineKind(llvm::EngineKind::JIT)
        .create();
    
    if (!llvm_engine_) {
        std::cerr << "JIT: Failed to create LLVM execution engine: " << error_str << std::endl;
        backend_ = Backend::ASMJIT_X64; // Fallback
    }
#endif
    
    std::cout << "JIT: Enhanced compiler initialized with optimization level " << optimization_level_ << std::endl;
}

JIT::~JIT() {
    if (trace_output_file_.is_open()) {
        flush_trace_buffer();
        trace_output_file_.close();
    }
    
    std::cout << "JIT: Statistics - Blocks: " << stats.blocks_compiled 
              << ", Instructions: " << stats.instructions_compiled
              << ", Cache hits: " << stats.cache_hits 
              << ", Cache misses: " << stats.cache_misses << std::endl;
              
    if (performance_counters_enabled_) {
        std::cout << "JIT: Performance - Total instructions: " << performance_counters_.total_instructions_executed
                  << ", IPC: " << performance_counters_.average_ipc
                  << ", Cache hit rate: " << (performance_counters_.cache_hit_rate * 100.0f) << "%"
                  << ", Branch accuracy: " << (performance_counters_.branch_prediction_accuracy * 100.0f) << "%" << std::endl;
    }
              
#ifdef PSX5_ENABLE_LLVM
    if (llvm_engine_) {
        delete llvm_engine_;
    }
#endif
}

std::optional<JIT::CompileResult> JIT::compile_sequence(const Memory& mem, uint64_t pc, CPU& cpu) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Check cache first
    auto cache_it = code_cache_.find(pc);
    if (cache_it != code_cache_.end()) {
        stats.cache_hits++;
        if (performance_counters_enabled_) {
            performance_counters_.cache_hits++;
        }
        return cache_it->second;
    }
    
    stats.cache_misses++;
    if (performance_counters_enabled_) {
        performance_counters_.cache_misses++;
    }
    
    // Update profiling data
    if (profiling_enabled_) {
        update_profiling_data(pc);
    }
    
    // Analyze instruction block using real x86-64 decoder
    InstructionBlock block = analyze_x86_block(mem, pc, cpu);
    if (block.x86_instructions.empty()) {
        return std::nullopt;
    }
    
    if (instruction_tracing_enabled_ && trace_level_ >= TraceLevel::DETAILED) {
        std::lock_guard<std::mutex> lock(trace_mutex_);
        for (const auto& instr : block.x86_instructions) {
            InstructionTrace trace;
            trace.pc = instr.pc;
            trace.opcode = instr.opcode;
            trace.timestamp_ns = get_current_timestamp_ns();
            trace.execution_count = 0; // Will be updated during execution
            instruction_trace_.push_back(trace);
        }
    }
    
    // Perform register allocation
    RegisterAllocator allocator;
    allocator.analyze_x86_liveness(block);
    if (!allocator.allocate_registers()) {
        std::cout << "JIT: Register allocation failed for block at 0x" << std::hex << pc << std::dec << std::endl;
        return std::nullopt;
    }
    
    // Apply optimizations based on level
    if (optimization_level_ >= 1) {
        optimize_constant_folding_x86(block);
        optimize_dead_code_elimination_x86(block);
        optimize_peephole_patterns(block);
    }
    
    if (optimization_level_ >= 2) {
        optimize_common_subexpression_elimination_x86(block);
        optimize_instruction_scheduling_x86(block);
        optimize_strength_reduction(block);
    }
    
    if (optimization_level_ >= 3 && is_hot_block(pc)) {
        optimize_loop_unrolling_x86(block);
        optimize_vectorization(block);
        optimize_branch_prediction(block);
    }
    
    // Generate code based on backend
    std::optional<CompileResult> result;
    
    switch (backend_) {
        case Backend::ASMJIT_X64:
            result = generate_asmjit_x86_code(block, allocator);
            break;
            
#ifdef PSX5_ENABLE_LLVM
        case Backend::LLVM_IR:
            result = generate_llvm_x86_code(block, allocator);
            break;
#endif
            
        case Backend::INTERPRETER:
        default:
            result = generate_interpreter_x86_code(block);
            break;
    }
    
    if (result) {
        // Cache the result
        code_cache_[pc] = *result;
        
        // Update statistics
        stats.blocks_compiled++;
        stats.instructions_compiled += block.x86_instructions.size();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        stats.compilation_time_us += duration.count();
        
        if (profiling_enabled_) {
            update_block_profile(block.start_pc, block.end_pc, 0, duration.count() * 1000);
        }
        
        std::cout << "JIT: Compiled block at 0x" << std::hex << pc << std::dec 
                  << " (" << block.x86_instructions.size() << " instructions, "
                  << duration.count() << "μs)" << std::endl;
    }
    
    return result;
}

void JIT::set_trace_output_file(const std::string& filename) {
    std::lock_guard<std::mutex> lock(trace_mutex_);
    if (trace_output_file_.is_open()) {
        trace_output_file_.close();
    }
    trace_output_file_.open(filename, std::ios::out | std::ios::binary);
    if (!trace_output_file_.is_open()) {
        std::cerr << "JIT: Failed to open trace output file: " << filename << std::endl;
    }
}

void JIT::flush_trace_buffer() {
    std::lock_guard<std::mutex> lock(trace_mutex_);
    if (trace_output_file_.is_open()) {
        for (const auto& trace : instruction_trace_) {
            write_trace_entry(trace);
        }
        trace_output_file_.flush();
    }
}

void JIT::record_instruction_trace(const InstructionBlock::X86Instruction& instr, const CPU& cpu_before, const CPU& cpu_after) {
    if (!instruction_tracing_enabled_) return;
    
    std::lock_guard<std::mutex> lock(trace_mutex_);
    
    InstructionTrace trace;
    trace.pc = instr.pc;
    trace.opcode = instr.opcode;
    trace.timestamp_ns = get_current_timestamp_ns();
    trace.execution_count = execution_counts_[instr.pc];
    trace.cycle_count = estimate_instruction_cycles(instr);
    
    if (trace_level_ >= TraceLevel::DETAILED) {
        // Record register states (simplified - would need actual CPU register access)
        // TODO: implement CPU register access
        trace.register_values_before.resize(16);
        trace.register_values_after.resize(16);
        // TODO: Copy actual register values from CPU state
    }
    
    if (trace_level_ >= TraceLevel::FULL) {
        // Record memory accesses
        if (instr.reads_memory) {
            // TODO: Record actual memory addresses read
            trace.memory_reads.push_back(instr.displacement);
        }
        if (instr.writes_memory) {
            // TODO: Record actual memory addresses written
            trace.memory_writes.push_back(instr.displacement);
        }
    }
    
    instruction_trace_.push_back(trace);
    
    // Keep trace buffer size manageable
    if (instruction_trace_.size() > 100000) {
        // Write oldest traces to file and remove from buffer
        for (size_t i = 0; i < 50000; ++i) {
            write_trace_entry(instruction_trace_[i]);
        }
        instruction_trace_.erase(instruction_trace_.begin(), instruction_trace_.begin() + 50000);
    }
}

void JIT::update_block_profile(uint64_t start_pc, uint64_t end_pc, uint64_t cycles, uint64_t time_ns) {
    auto& profile = block_profiles_[start_pc];
    profile.start_pc = start_pc;
    profile.end_pc = end_pc;
    profile.execution_count++;
    profile.total_cycles += cycles;
    profile.total_time_ns += time_ns;
    
    // Calculate average IPC
    if (profile.total_cycles > 0) {
        size_t instruction_count = (end_pc - start_pc) / 4; // Approximate
        profile.average_ipc = static_cast<float>(instruction_count * profile.execution_count) / profile.total_cycles;
    }
    
    // Mark as hot path if executed frequently
    profile.is_hot_path = profile.execution_count > 1000;
}

void JIT::update_performance_counters(const InstructionBlock::X86Instruction& instr, bool cache_hit, bool branch_taken) {
    if (!performance_counters_enabled_) return;
    
    performance_counters_.total_instructions_executed++;
    performance_counters_.total_cycles += estimate_instruction_cycles(instr);
    
    if (cache_hit) {
        performance_counters_.cache_hits++;
    } else {
        performance_counters_.cache_misses++;
    }
    
    if (instr.reads_memory) {
        performance_counters_.memory_loads++;
    }
    if (instr.writes_memory) {
        performance_counters_.memory_stores++;
    }
    
    if (instr.is_branch) {
        // Simplified branch prediction tracking
        if (branch_taken) {
            performance_counters_.branch_predictions_correct++;
        } else {
            performance_counters_.branch_predictions_incorrect++;
        }
    }
    
    // Update derived metrics
    if (performance_counters_.total_cycles > 0) {
        performance_counters_.average_ipc = static_cast<float>(performance_counters_.total_instructions_executed) / performance_counters_.total_cycles;
    }
    
    uint64_t total_cache_accesses = performance_counters_.cache_hits + performance_counters_.cache_misses;
    if (total_cache_accesses > 0) {
        performance_counters_.cache_hit_rate = static_cast<float>(performance_counters_.cache_hits) / total_cache_accesses;
    }
    
    uint64_t total_branches = performance_counters_.branch_predictions_correct + performance_counters_.branch_predictions_incorrect;
    if (total_branches > 0) {
        performance_counters_.branch_prediction_accuracy = static_cast<float>(performance_counters_.branch_predictions_correct) / total_branches;
    }
}

void JIT::write_trace_entry(const InstructionTrace& trace) {
    if (!trace_output_file_.is_open()) return;
    
    // Write binary trace entry
    trace_output_file_.write(reinterpret_cast<const char*>(&trace.pc), sizeof(trace.pc));
    trace_output_file_.write(reinterpret_cast<const char*>(&trace.opcode), sizeof(trace.opcode));
    trace_output_file_.write(reinterpret_cast<const char*>(&trace.timestamp_ns), sizeof(trace.timestamp_ns));
    trace_output_file_.write(reinterpret_cast<const char*>(&trace.execution_count), sizeof(trace.execution_count));
    trace_output_file_.write(reinterpret_cast<const char*>(&trace.cycle_count), sizeof(trace.cycle_count));
    
    // Write register values if available
    if (!trace.register_values_before.empty()) {
        uint32_t reg_count = trace.register_values_before.size();
        trace_output_file_.write(reinterpret_cast<const char*>(&reg_count), sizeof(reg_count));
        trace_output_file_.write(reinterpret_cast<const char*>(trace.register_values_before.data()), reg_count);
        trace_output_file_.write(reinterpret_cast<const char*>(trace.register_values_after.data()), reg_count);
    }
    
    // Write memory accesses
    uint32_t read_count = trace.memory_reads.size();
    uint32_t write_count = trace.memory_writes.size();
    trace_output_file_.write(reinterpret_cast<const char*>(&read_count), sizeof(read_count));
    trace_output_file_.write(reinterpret_cast<const char*>(&write_count), sizeof(write_count));
    
    if (read_count > 0) {
        trace_output_file_.write(reinterpret_cast<const char*>(trace.memory_reads.data()), read_count * sizeof(uint64_t));
    }
    if (write_count > 0) {
        trace_output_file_.write(reinterpret_cast<const char*>(trace.memory_writes.data()), write_count * sizeof(uint64_t));
    }
}

uint64_t JIT::get_current_timestamp_ns() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now - start_time_;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

uint64_t JIT::estimate_instruction_cycles(const InstructionBlock::X86Instruction& instr) const {
    // Simplified cycle estimation based on instruction type
    // TODO: implement more accurate cycle estimation
    if (instr.reads_memory && instr.writes_memory) return 4; // Memory-to-memory
    if (instr.reads_memory || instr.writes_memory) return 3;  // Memory access
    if (instr.is_branch) return instr.is_conditional ? 2 : 1; // Branch prediction cost
    if (instr.opcode == 0xF7) return 8; // MUL/DIV operations
    if (instr.uses_sse) return 2; // SIMD operations
    return 1; // Simple ALU operations
}

void JIT::reset_performance_counters() {
    std::lock_guard<std::mutex> lock(trace_mutex_);
    memset(&performance_counters_, 0, sizeof(performance_counters_));
}

void JIT::dump_performance_report(const std::string& filename) {
    std::ofstream report(filename);
    if (!report.is_open()) {
        std::cerr << "JIT: Failed to open performance report file: " << filename << std::endl;
        return;
    }
    
    report << "PS5 Emulator JIT Performance Report\n";
    report << "===================================\n\n";
    
    report << "Execution Statistics:\n";
    report << "  Total Instructions: " << performance_counters_.total_instructions_executed << "\n";
    report << "  Total Cycles: " << performance_counters_.total_cycles << "\n";
    report << "  Average IPC: " << performance_counters_.average_ipc << "\n\n";
    
    report << "Cache Performance:\n";
    report << "  Cache Hits: " << performance_counters_.cache_hits << "\n";
    report << "  Cache Misses: " << performance_counters_.cache_misses << "\n";
    report << "  Hit Rate: " << (performance_counters_.cache_hit_rate * 100.0f) << "%\n\n";
    
    report << "Branch Prediction:\n";
    report << "  Correct Predictions: " << performance_counters_.branch_predictions_correct << "\n";
    report << "  Incorrect Predictions: " << performance_counters_.branch_predictions_incorrect << "\n";
    report << "  Accuracy: " << (performance_counters_.branch_prediction_accuracy * 100.0f) << "%\n\n";
    
    report << "Memory Operations:\n";
    report << "  Loads: " << performance_counters_.memory_loads << "\n";
    report << "  Stores: " << performance_counters_.memory_stores << "\n";
    report << "  Exceptions: " << performance_counters_.exceptions_raised << "\n\n";
    
    report << "Hot Blocks (>1000 executions):\n";
    for (const auto& [pc, profile] : block_profiles_) {
        if (profile.is_hot_path) {
            report << "  0x" << std::hex << pc << std::dec 
                   << " - " << profile.execution_count << " executions, "
                   << "IPC: " << profile.average_ipc << "\n";
        }
    }
    
    report.close();
    std::cout << "JIT: Performance report written to " << filename << std::endl;
}

std::vector<uint64_t> JIT::get_hot_blocks(float threshold) const {
    std::vector<uint64_t> hot_blocks;
    
    // Find maximum execution count
    uint32_t max_executions = 0;
    for (const auto& [pc, profile] : block_profiles_) {
        max_executions = std::max(max_executions, profile.execution_count);
    }
    
    // Collect blocks above threshold
    uint32_t threshold_count = static_cast<uint32_t>(max_executions * threshold);
    for (const auto& [pc, profile] : block_profiles_) {
        if (profile.execution_count >= threshold_count) {
            hot_blocks.push_back(pc);
        }
    }
    
    return hot_blocks;
}

JIT::InstructionBlock JIT::analyze_x86_block(const Memory& mem, uint64_t start_pc, CPU& cpu, uint64_t max_size) {
    InstructionBlock block;
    block.start_pc = start_pc;
    block.has_branches = false;
    block.has_calls = false;
    block.has_memory_ops = false;
    block.execution_count = execution_counts_[start_pc];
    
    uint64_t current_pc = start_pc;
    size_t bytes_analyzed = 0;
    std::unordered_set<uint64_t> branch_targets;
    
    // First pass: decode instructions and identify basic block boundaries
    while (bytes_analyzed < max_size && current_pc < mem.size()) {
        CPU::Instruction instr = cpu.decode_instruction(current_pc);
        
        X86Instruction x86_instr;
        x86_instr.pc = current_pc;
        x86_instr.opcode = instr.opcode;
        x86_instr.length = instr.length;
        x86_instr.modrm = instr.modrm;
        x86_instr.sib = instr.sib;
        x86_instr.rex = instr.rex;
        x86_instr.immediate = instr.immediate;
        x86_instr.displacement = instr.displacement;
        x86_instr.prefixes = instr.prefixes;
        
        // Analyze instruction properties
        analyze_instruction_properties(x86_instr, block);
        
        block.x86_instructions.push_back(x86_instr);
        
        // Check for block terminators
        if (is_block_terminator(instr.opcode)) {
            if (is_unconditional_branch(instr.opcode)) {
                // Calculate branch target
                uint64_t target = current_pc + instr.length + instr.immediate;
                branch_targets.insert(target);
                block.end_pc = current_pc + instr.length;
                break;
            } else if (is_conditional_branch(instr.opcode)) {
                uint64_t target = current_pc + instr.length + instr.immediate;
                branch_targets.insert(target);
                block.has_branches = true;
            } else if (instr.opcode == 0xC3 || instr.opcode == 0xCB) { // RET/RETF
                block.end_pc = current_pc + instr.length;
                break;
            }
        }
        
        current_pc += instr.length;
        bytes_analyzed += instr.length;
        
        // Stop at maximum instruction count for compilation unit
        if (block.x86_instructions.size() >= 100) {
            break;
        }
    }
    
    if (block.end_pc == 0) {
        block.end_pc = current_pc;
    }
    
    // Store branch targets for optimization
    block.branch_targets = std::vector<uint64_t>(branch_targets.begin(), branch_targets.end());
    
    return block;
}

void JIT::analyze_instruction_properties(X86Instruction& instr, InstructionBlock& block) {
    switch (instr.opcode) {
        // Memory operations
        case 0x8B: // MOV reg, r/m
        case 0x89: // MOV r/m, reg
        case 0xA1: // MOV EAX, moffs
        case 0xA3: // MOV moffs, EAX
            block.has_memory_ops = true;
            instr.reads_memory = (instr.opcode == 0x8B || instr.opcode == 0xA1);
            instr.writes_memory = (instr.opcode == 0x89 || instr.opcode == 0xA3);
            break;
            
        // Arithmetic operations
        case 0x01: // ADD r/m, reg
        case 0x03: // ADD reg, r/m
        case 0x29: // SUB r/m, reg
        case 0x2B: // SUB reg, r/m
        case 0x0F: // Two-byte opcodes
            instr.is_arithmetic = true;
            break;
            
        // Control flow
        case 0xE8: // CALL
            block.has_calls = true;
            instr.is_call = true;
            break;
        case 0xE9: // JMP rel32
        case 0xEB: // JMP rel8
            instr.is_branch = true;
            instr.is_unconditional = true;
            break;
        case 0x0F84: // JE
        case 0x0F85: // JNE
        case 0x0F8C: // JL
        case 0x0F8F: // JG
            block.has_branches = true;
            instr.is_branch = true;
            instr.is_conditional = true;
            break;
    }
    
    // Analyze register usage
    analyze_register_usage(instr);
}

void JIT::analyze_register_usage(X86Instruction& instr) {
    uint8_t mod = (instr.modrm >> 6) & 3;
    uint8_t reg = (instr.modrm >> 3) & 7;
    uint8_t rm = instr.modrm & 7;
    
    // Handle REX prefix extensions
    if (instr.rex & 0x04) reg += 8;
    if (instr.rex & 0x01) rm += 8;
    
    switch (instr.opcode) {
        case 0x89: // MOV r/m, reg
            instr.registers_read.insert(reg);
            if (mod == 3) {
                instr.registers_written.insert(rm);
            }
            break;
        case 0x8B: // MOV reg, r/m
            instr.registers_written.insert(reg);
            if (mod == 3) {
                instr.registers_read.insert(rm);
            }
            break;
        case 0x01: // ADD r/m, reg
        case 0x29: // SUB r/m, reg
            instr.registers_read.insert(reg);
            if (mod == 3) {
                instr.registers_read.insert(rm);
                instr.registers_written.insert(rm);
            }
            break;
    }
}

void JIT::RegisterAllocator::analyze_x86_liveness(const InstructionBlock& block) {
    live_ranges.clear();
    physical_reg_used.assign(16, false);
    virtual_to_physical.clear();
    
    std::unordered_map<uint8_t, size_t> last_use;
    std::unordered_map<uint8_t, size_t> first_def;
    
    // Analyze register liveness across x86 instructions
    for (size_t i = 0; i < block.x86_instructions.size(); ++i) {
        const auto& instr = block.x86_instructions[i];
        
        // Track register reads
        for (uint8_t reg : instr.registers_read) {
            last_use[reg] = i;
        }
        
        // Track register writes
        for (uint8_t reg : instr.registers_written) {
            if (first_def.find(reg) == first_def.end()) {
                first_def[reg] = i;
            }
            last_use[reg] = i;
        }
    }
    
    // Create live ranges for each register
    for (const auto& [reg, first] : first_def) {
        LiveRange range;
        range.virtual_reg = reg;
        range.start_pc = first;
        range.end_pc = last_use[reg];
        range.physical_reg = 0xFF;
        range.spilled = false;
        range.spill_cost = calculate_spill_cost(reg, block);
        live_ranges.push_back(range);
    }
    
    // Sort by start PC for linear scan
    std::sort(live_ranges.begin(), live_ranges.end(), 
              [](const LiveRange& a, const LiveRange& b) {
                  return a.start_pc < b.start_pc;
              });
}

float JIT::RegisterAllocator::calculate_spill_cost(uint8_t reg, const InstructionBlock& block) {
    float cost = 0.0f;
    
    for (const auto& instr : block.x86_instructions) {
        // Higher cost for frequently used registers
        if (instr.registers_read.count(reg) || instr.registers_written.count(reg)) {
            cost += 1.0f;
            
            // Extra cost for memory operations (spilling would add more memory traffic)
            if (instr.reads_memory || instr.writes_memory) {
                cost += 2.0f;
            }
            
            // Extra cost in loops (based on execution count)
            cost += block.execution_count * 0.1f;
        }
    }
    
    return cost;
}

bool JIT::RegisterAllocator::allocate_registers() {
    physical_reg_used.assign(16, false);
    virtual_to_physical.clear();
    
    // Available x86-64 registers for allocation (excluding RSP, RBP)
    std::vector<uint8_t> available_regs = {
        0,  // RAX
        1,  // RCX  
        2,  // RDX
        3,  // RBX
        6,  // RSI
        7,  // RDI
        8,  // R8
        9,  // R9
        10, // R10
        11, // R11
        12, // R12
        13, // R13
        14, // R14
        15  // R15
    };
    
    // Active intervals (currently allocated)
    std::vector<LiveRange*> active;
    
    // Sort live ranges by start point
    std::sort(live_ranges.begin(), live_ranges.end(),
              [](const LiveRange& a, const LiveRange& b) {
                  return a.start_pc < b.start_pc;
              });
    
    for (auto& range : live_ranges) {
        // Expire old intervals
        auto it = active.begin();
        while (it != active.end()) {
            if ((*it)->end_pc <= range.start_pc) {
                // Free the register
                physical_reg_used[(*it)->physical_reg] = false;
                it = active.erase(it);
            } else {
                ++it;
            }
        }
        
        // Try to allocate a register
        bool allocated = false;
        for (uint8_t reg : available_regs) {
            if (!physical_reg_used[reg]) {
                range.physical_reg = reg;
                range.spilled = false;
                physical_reg_used[reg] = true;
                virtual_to_physical[range.virtual_reg] = reg;
                active.push_back(&range);
                allocated = true;
                break;
            }
        }
        
        // If no register available, spill
        if (!allocated) {
            // Find the interval that ends furthest in the future
            auto spill_candidate = std::max_element(active.begin(), active.end(),
                [](const LiveRange* a, const LiveRange* b) {
                    return a->end_pc < b->end_pc;
                });
            
            if (spill_candidate != active.end() && (*spill_candidate)->end_pc > range.end_pc) {
                // Spill the candidate and use its register
                range.physical_reg = (*spill_candidate)->physical_reg;
                range.spilled = false;
                virtual_to_physical[range.virtual_reg] = range.physical_reg;
                
                // Mark the spilled range
                (*spill_candidate)->spilled = true;
                virtual_to_physical.erase((*spill_candidate)->virtual_reg);
                
                // Replace in active list
                *spill_candidate = &range;
            } else {
                // Spill current range
                range.spilled = true;
                range.physical_reg = 0xFF;
            }
        }
    }
    
    return true; // Always succeed, spilling if necessary
}

uint8_t JIT::RegisterAllocator::get_physical_register(uint8_t virtual_reg) {
    auto it = virtual_to_physical.find(virtual_reg);
    if (it != virtual_to_physical.end()) {
        return it->second;
    }
    
    // Register was spilled, need to load from memory
    return 0xFF; // Indicates spilled register
}

void JIT::RegisterAllocator::spill_register(uint8_t virtual_reg) {
    auto it = virtual_to_physical.find(virtual_reg);
    if (it != virtual_to_physical.end()) {
        physical_reg_used[it->second] = false;
        virtual_to_physical.erase(it);
    }
    
    // Mark as spilled in live ranges
    for (auto& range : live_ranges) {
        if (range.virtual_reg == virtual_reg) {
            range.spilled = true;
            range.physical_reg = 0xFF;
            break;
        }
    }
}

void JIT::RegisterAllocator::reset() {
    live_ranges.clear();
    physical_reg_used.assign(16, false);
    virtual_to_physical.clear();
    spill_slots.clear();
    next_spill_slot = 0;
}

int32_t JIT::RegisterAllocator::get_spill_slot(uint8_t virtual_reg) {
    auto it = spill_slots.find(virtual_reg);
    if (it != spill_slots.end()) {
        return it->second;
    }
    
    // Allocate new spill slot (8 bytes per slot, negative offset from RBP)
    int32_t slot = -(++next_spill_slot * 8);
    spill_slots[virtual_reg] = slot;
    return slot;
}

JIT::InstructionBlock JIT::analyze_x86_block(const Memory& mem, uint64_t start_pc, CPU& cpu, uint64_t max_size) {
    InstructionBlock block;
    block.start_pc = start_pc;
    block.has_branches = false;
    block.has_calls = false;
    block.has_memory_ops = false;
    block.execution_count = execution_counts_[start_pc];
    
    uint64_t current_pc = start_pc;
    size_t bytes_analyzed = 0;
    std::unordered_set<uint64_t> branch_targets;
    
    // First pass: decode instructions and identify basic block boundaries
    while (bytes_analyzed < max_size && current_pc < mem.size()) {
        CPU::Instruction instr = cpu.decode_instruction(current_pc);
        
        X86Instruction x86_instr;
        x86_instr.pc = current_pc;
        x86_instr.opcode = instr.opcode;
        x86_instr.length = instr.length;
        x86_instr.modrm = instr.modrm;
        x86_instr.sib = instr.sib;
        x86_instr.rex = instr.rex;
        x86_instr.immediate = instr.immediate;
        x86_instr.displacement = instr.displacement;
        x86_instr.prefixes = instr.prefixes;
        
        // Analyze instruction properties
        analyze_instruction_properties(x86_instr, block);
        
        block.x86_instructions.push_back(x86_instr);
        
        // Check for block terminators
        if (is_block_terminator(instr.opcode)) {
            if (is_unconditional_branch(instr.opcode)) {
                // Calculate branch target
                uint64_t target = current_pc + instr.length + instr.immediate;
                branch_targets.insert(target);
                block.end_pc = current_pc + instr.length;
                break;
            } else if (is_conditional_branch(instr.opcode)) {
                uint64_t target = current_pc + instr.length + instr.immediate;
                branch_targets.insert(target);
                block.has_branches = true;
            } else if (instr.opcode == 0xC3 || instr.opcode == 0xCB) { // RET/RETF
                block.end_pc = current_pc + instr.length;
                break;
            }
        }
        
        current_pc += instr.length;
        bytes_analyzed += instr.length;
        
        // Stop at maximum instruction count for compilation unit
        if (block.x86_instructions.size() >= 100) {
            break;
        }
    }
    
    if (block.end_pc == 0) {
        block.end_pc = current_pc;
    }
    
    // Store branch targets for optimization
    block.branch_targets = std::vector<uint64_t>(branch_targets.begin(), branch_targets.end());
    
    return block;
}

void JIT::analyze_instruction_properties(X86Instruction& instr, InstructionBlock& block) {
    switch (instr.opcode) {
        // Memory operations
        case 0x8B: // MOV reg, r/m
        case 0x89: // MOV r/m, reg
        case 0xA1: // MOV EAX, moffs
        case 0xA3: // MOV moffs, EAX
            block.has_memory_ops = true;
            instr.reads_memory = (instr.opcode == 0x8B || instr.opcode == 0xA1);
            instr.writes_memory = (instr.opcode == 0x89 || instr.opcode == 0xA3);
            break;
            
        // Arithmetic operations
        case 0x01: // ADD r/m, reg
        case 0x03: // ADD reg, r/m
        case 0x29: // SUB r/m, reg
        case 0x2B: // SUB reg, r/m
        case 0x0F: // Two-byte opcodes
            instr.is_arithmetic = true;
            break;
            
        // Control flow
        case 0xE8: // CALL
            block.has_calls = true;
            instr.is_call = true;
            break;
        case 0xE9: // JMP rel32
        case 0xEB: // JMP rel8
            instr.is_branch = true;
            instr.is_unconditional = true;
            break;
        case 0x0F84: // JE
        case 0x0F85: // JNE
        case 0x0F8C: // JL
        case 0x0F8F: // JG
            block.has_branches = true;
            instr.is_branch = true;
            instr.is_conditional = true;
            break;
    }
    
    // Analyze register usage
    analyze_register_usage(instr);
}

void JIT::analyze_register_usage(X86Instruction& instr) {
    uint8_t mod = (instr.modrm >> 6) & 3;
    uint8_t reg = (instr.modrm >> 3) & 7;
    uint8_t rm = instr.modrm & 7;
    
    // Handle REX prefix extensions
    if (instr.rex & 0x04) reg += 8;
    if (instr.rex & 0x01) rm += 8;
    
    switch (instr.opcode) {
        case 0x89: // MOV r/m, reg
            instr.registers_read.insert(reg);
            if (mod == 3) {
                instr.registers_written.insert(rm);
            }
            break;
        case 0x8B: // MOV reg, r/m
            instr.registers_written.insert(reg);
            if (mod == 3) {
                instr.registers_read.insert(rm);
            }
            break;
        case 0x01: // ADD r/m, reg
        case 0x29: // SUB r/m, reg
            instr.registers_read.insert(reg);
            if (mod == 3) {
                instr.registers_read.insert(rm);
                instr.registers_written.insert(rm);
            }
            break;
    }
}

void JIT::RegisterAllocator::analyze_x86_liveness(const InstructionBlock& block) {
    live_ranges.clear();
    physical_reg_used.assign(16, false);
    virtual_to_physical.clear();
    
    std::unordered_map<uint8_t, size_t> last_use;
    std::unordered_map<uint8_t, size_t> first_def;
    
    // Analyze register liveness across x86 instructions
    for (size_t i = 0; i < block.x86_instructions.size(); ++i) {
        const auto& instr = block.x86_instructions[i];
        
        // Track register reads
        for (uint8_t reg : instr.registers_read) {
            last_use[reg] = i;
        }
        
        // Track register writes
        for (uint8_t reg : instr.registers_written) {
            if (first_def.find(reg) == first_def.end()) {
                first_def[reg] = i;
            }
            last_use[reg] = i;
        }
    }
    
    // Create live ranges for each register
    for (const auto& [reg, first] : first_def) {
        LiveRange range;
        range.virtual_reg = reg;
        range.start_pc = first;
        range.end_pc = last_use[reg];
        range.physical_reg = 0xFF;
        range.spilled = false;
        range.spill_cost = calculate_spill_cost(reg, block);
        live_ranges.push_back(range);
    }
    
    // Sort by start PC for linear scan
    std::sort(live_ranges.begin(), live_ranges.end(), 
              [](const LiveRange& a, const LiveRange& b) {
                  return a.start_pc < b.start_pc;
              });
}

float JIT::RegisterAllocator::calculate_spill_cost(uint8_t reg, const InstructionBlock& block) {
    float cost = 0.0f;
    
    for (const auto& instr : block.x86_instructions) {
        // Higher cost for frequently used registers
        if (instr.registers_read.count(reg) || instr.registers_written.count(reg)) {
            cost += 1.0f;
            
            // Extra cost for memory operations (spilling would add more memory traffic)
            if (instr.reads_memory || instr.writes_memory) {
                cost += 2.0f;
            }
            
            // Extra cost in loops (based on execution count)
            cost += block.execution_count * 0.1f;
        }
    }
    
    return cost;
}

bool JIT::RegisterAllocator::allocate_registers() {
    physical_reg_used.assign(16, false);
    virtual_to_physical.clear();
    
    // Available x86-64 registers for allocation (excluding RSP, RBP)
    std::vector<uint8_t> available_regs = {
        0,  // RAX
        1,  // RCX  
        2,  // RDX
        3,  // RBX
        6,  // RSI
        7,  // RDI
        8,  // R8
        9,  // R9
        10, // R10
        11, // R11
        12, // R12
        13, // R13
        14, // R14
        15  // R15
    };
    
    // Active intervals (currently allocated)
    std::vector<LiveRange*> active;
    
    // Sort live ranges by start point
    std::sort(live_ranges.begin(), live_ranges.end(),
              [](const LiveRange& a, const LiveRange& b) {
                  return a.start_pc < b.start_pc;
              });
    
    for (auto& range : live_ranges) {
        // Expire old intervals
        auto it = active.begin();
        while (it != active.end()) {
            if ((*it)->end_pc <= range.start_pc) {
                // Free the register
                physical_reg_used[(*it)->physical_reg] = false;
                it = active.erase(it);
            } else {
                ++it;
            }
        }
        
        // Try to allocate a register
        bool allocated = false;
        for (uint8_t reg : available_regs) {
            if (!physical_reg_used[reg]) {
                range.physical_reg = reg;
                range.spilled = false;
                physical_reg_used[reg] = true;
                virtual_to_physical[range.virtual_reg] = reg;
                active.push_back(&range);
                allocated = true;
                break;
            }
        }
        
        // If no register available, spill
        if (!allocated) {
            // Find the interval that ends furthest in the future
            auto spill_candidate = std::max_element(active.begin(), active.end(),
                [](const LiveRange* a, const LiveRange* b) {
                    return a->end_pc < b->end_pc;
                });
            
            if (spill_candidate != active.end() && (*spill_candidate)->end_pc > range.end_pc) {
                // Spill the candidate and use its register
                range.physical_reg = (*spill_candidate)->physical_reg;
                range.spilled = false;
                virtual_to_physical[range.virtual_reg] = range.physical_reg;
                
                // Mark the spilled range
                (*spill_candidate)->spilled = true;
                virtual_to_physical.erase((*spill_candidate)->virtual_reg);
                
                // Replace in active list
                *spill_candidate = &range;
            } else {
                // Spill current range
                range.spilled = true;
                range.physical_reg = 0xFF;
            }
        }
    }
    
    return true; // Always succeed, spilling if necessary
}
