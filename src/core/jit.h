#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>
#include <unordered_map>
#include <memory>
#include <unordered_set>
#include <chrono>
#include <fstream>
#include <mutex>

class Memory;
class CPU;

class JIT {
public:
    struct CompileResult {
        std::function<void(CPU&)> invoke;
        size_t bytes_consumed;
        uint32_t optimization_level;
        std::vector<uint32_t> register_usage;
    };

    struct InstructionTrace {
        uint64_t pc;
        uint16_t opcode;
        uint64_t timestamp_ns;
        uint32_t execution_count;
        uint64_t cycle_count;
        std::vector<uint8_t> register_values_before;
        std::vector<uint8_t> register_values_after;
        std::vector<uint64_t> memory_reads;
        std::vector<uint64_t> memory_writes;
        bool caused_exception;
        uint32_t exception_code;
        float execution_time_us;
    };

    struct BlockProfile {
        uint64_t start_pc;
        uint64_t end_pc;
        uint32_t execution_count;
        uint64_t total_cycles;
        uint64_t total_time_ns;
        float average_ipc; // Instructions per cycle
        std::vector<uint32_t> instruction_frequencies;
        std::unordered_map<uint16_t, uint32_t> opcode_histogram;
        uint32_t cache_misses;
        uint32_t branch_mispredictions;
        bool is_hot_path;
    };

    struct PerformanceCounters {
        uint64_t total_instructions_executed;
        uint64_t total_cycles;
        uint64_t cache_hits;
        uint64_t cache_misses;
        uint64_t branch_predictions_correct;
        uint64_t branch_predictions_incorrect;
        uint64_t memory_loads;
        uint64_t memory_stores;
        uint64_t exceptions_raised;
        uint64_t context_switches;
        float average_ipc;
        float cache_hit_rate;
        float branch_prediction_accuracy;
    };

    enum class TraceLevel {
        NONE = 0,
        BASIC = 1,      // PC and opcode only
        DETAILED = 2,   // Include register states
        FULL = 3        // Include memory accesses and timing
    };

    // Register allocation and optimization
    struct RegisterAllocator {
        struct LiveRange {
            size_t start_pc;
            size_t end_pc;
            uint8_t virtual_reg;
            uint8_t physical_reg;
            bool spilled;
            float spill_cost;
            uint32_t use_count;
            bool is_loop_carried;
        };
        
        std::vector<LiveRange> live_ranges;
        std::vector<bool> physical_reg_used;
        std::unordered_map<uint8_t, uint8_t> virtual_to_physical;
        std::unordered_map<uint8_t, int32_t> spill_slots;
        int32_t next_spill_slot;
        
        void analyze_liveness(const std::vector<uint8_t>& bytecode, size_t start_pc);
        void analyze_x86_liveness(const InstructionBlock& block);
        bool allocate_registers();
        uint8_t get_physical_register(uint8_t virtual_reg);
        void spill_register(uint8_t virtual_reg);
        float calculate_spill_cost(uint8_t reg, const InstructionBlock& block);
        int32_t get_spill_slot(uint8_t virtual_reg);
        void reset();
    };
    
    // Instruction analysis and optimization
    struct InstructionBlock {
        uint64_t start_pc;
        uint64_t end_pc;
        std::vector<uint8_t> bytecode;
        std::vector<uint32_t> instruction_offsets;
        bool has_branches;
        bool has_calls;
        bool has_memory_ops;
        uint32_t execution_count;
        std::vector<uint64_t> branch_targets; // Added branch targets for tracing
        
        struct X86Instruction {
            uint64_t pc; // Added PC for tracing
            uint16_t opcode;
            uint8_t modrm;
            uint8_t sib;
            uint8_t rex;
            uint8_t prefixes;
            uint8_t segment_override;
            int32_t displacement;
            uint64_t immediate;
            uint8_t length;
            std::unordered_set<uint8_t> registers_read;
            std::unordered_set<uint8_t> registers_written;
            bool reads_memory;
            bool writes_memory;
            bool is_call;
            bool is_branch;
            bool is_conditional;
            bool is_unconditional;
            bool is_arithmetic;
            bool uses_sse;
            bool uses_fma;
            uint8_t secondary_opcode;
            std::string optimization_hint;
        };
        std::vector<X86Instruction> x86_instructions;
    };
    
    // Code generation backends
    enum class Backend {
        INTERPRETER,
        ASMJIT_X64,
        LLVM_IR,
        CUSTOM_NATIVE
    };
    
    JIT();
    ~JIT();
    
    std::optional<CompileResult> compile_sequence(const Memory& mem, uint64_t pc, CPU& cpu);
    std::optional<CompileResult> compile_block(const Memory& mem, uint64_t start_pc, uint64_t end_pc, CPU& cpu);
    
    void set_optimization_level(uint32_t level) { optimization_level_ = level; }
    void set_backend(Backend backend) { backend_ = backend; }
    void enable_profiling(bool enable) { profiling_enabled_ = enable; }
    
    void set_trace_level(TraceLevel level) { trace_level_ = level; }
    void enable_instruction_tracing(bool enable) { instruction_tracing_enabled_ = enable; }
    void enable_performance_counters(bool enable) { performance_counters_enabled_ = enable; }
    void set_trace_output_file(const std::string& filename);
    void flush_trace_buffer();
    
    // Profiling and analysis
    const std::vector<InstructionTrace>& get_instruction_trace() const { return instruction_trace_; }
    const std::unordered_map<uint64_t, BlockProfile>& get_block_profiles() const { return block_profiles_; }
    const PerformanceCounters& get_performance_counters() const { return performance_counters_; }
    void reset_performance_counters();
    void dump_performance_report(const std::string& filename);
    std::vector<uint64_t> get_hot_blocks(float threshold = 0.8f) const;
    
    // Code cache management
    void invalidate_cache();
    void invalidate_range(uint64_t start, uint64_t end);
    size_t get_cache_size() const { return code_cache_.size(); }
    
    // Performance statistics
    struct Statistics {
        uint64_t blocks_compiled;
        uint64_t instructions_compiled;
        uint64_t cache_hits;
        uint64_t cache_misses;
        uint64_t optimization_time_us;
        uint64_t compilation_time_us;
    } stats;
    
private:
    Backend backend_;
    uint32_t optimization_level_;
    bool profiling_enabled_;
    
    TraceLevel trace_level_;
    bool instruction_tracing_enabled_;
    bool performance_counters_enabled_;
    std::vector<InstructionTrace> instruction_trace_;
    std::unordered_map<uint64_t, BlockProfile> block_profiles_;
    PerformanceCounters performance_counters_;
    std::ofstream trace_output_file_;
    std::mutex trace_mutex_;
    std::chrono::high_resolution_clock::time_point start_time_;
    
    std::unordered_map<uint64_t, CompileResult> code_cache_;
    
    // Optimization passes
    void optimize_constant_folding(InstructionBlock& block);
    void optimize_dead_code_elimination(InstructionBlock& block);
    void optimize_common_subexpression_elimination(InstructionBlock& block);
    void optimize_loop_unrolling(InstructionBlock& block);
    void optimize_instruction_scheduling(InstructionBlock& block);
    void optimize_constant_folding_x86(InstructionBlock& block);
    void optimize_dead_code_elimination_x86(InstructionBlock& block);
    void optimize_common_subexpression_elimination_x86(InstructionBlock& block);
    void optimize_instruction_scheduling_x86(InstructionBlock& block);
    void optimize_loop_unrolling_x86(InstructionBlock& block);
    void optimize_vectorization_x86(InstructionBlock& block);
    void optimize_strength_reduction_x86(InstructionBlock& block);
    void optimize_peephole_patterns(InstructionBlock& block);
    void optimize_branch_prediction(InstructionBlock& block);
    
    // Code generation
    std::optional<CompileResult> generate_asmjit_code(const InstructionBlock& block, RegisterAllocator& allocator);
    std::optional<CompileResult> generate_llvm_code(const InstructionBlock& block, RegisterAllocator& allocator);
    std::optional<CompileResult> generate_interpreter_code(const InstructionBlock& block);
    std::optional<CompileResult> generate_asmjit_x86_code(const InstructionBlock& block, RegisterAllocator& allocator);
    std::optional<CompileResult> generate_llvm_x86_code(const InstructionBlock& block, RegisterAllocator& allocator);
    std::optional<CompileResult> generate_interpreter_x86_code(const InstructionBlock& block);
    
    // Analysis
    InstructionBlock analyze_block(const Memory& mem, uint64_t start_pc, uint64_t max_size = 1024);
    InstructionBlock analyze_x86_block(const Memory& mem, uint64_t start_pc, CPU& cpu, uint64_t max_size = 1024);
    void analyze_instruction_properties(InstructionBlock::X86Instruction& instr, InstructionBlock& block);
    void analyze_register_usage(InstructionBlock::X86Instruction& instr);
    void analyze_x86_instruction_properties(InstructionBlock::X86Instruction& instr, const uint8_t* code);
    size_t get_x86_operand_length(const InstructionBlock::X86Instruction& instr, const uint8_t* code);
    bool is_hot_block(uint64_t pc) const;
    bool is_block_terminator(uint16_t opcode) const;
    bool is_unconditional_branch(uint16_t opcode) const;
    bool is_conditional_branch(uint16_t opcode) const;
    
    void update_profiling_data(uint64_t pc);
    void record_instruction_trace(const InstructionBlock::X86Instruction& instr, const CPU& cpu_before, const CPU& cpu_after);
    void update_block_profile(uint64_t start_pc, uint64_t end_pc, uint64_t cycles, uint64_t time_ns);
    void update_performance_counters(const InstructionBlock::X86Instruction& instr, bool cache_hit, bool branch_taken);
    void write_trace_entry(const InstructionTrace& trace);
    uint64_t get_current_timestamp_ns() const;
    uint64_t estimate_instruction_cycles(const InstructionBlock::X86Instruction& instr) const;
    
    // Profiling
    std::unordered_map<uint64_t, uint32_t> execution_counts_;
};

// x86-64 specific instruction encoding and optimization
class X64CodeGenerator {
public:
    struct X64Instruction {
        uint8_t opcode;
        uint8_t modrm;
        uint8_t sib;
        uint8_t rex;
        uint32_t displacement;
        uint64_t immediate;
        uint8_t length;
    };
    
    X64CodeGenerator();
    
    // Code generation
    std::vector<uint8_t> generate_prologue();
    std::vector<uint8_t> generate_epilogue();
    std::vector<uint8_t> generate_mov_reg_imm(uint8_t reg, uint64_t imm);
    std::vector<uint8_t> generate_add_reg_imm(uint8_t reg, uint64_t imm);
    std::vector<uint8_t> generate_mul_reg_imm(uint8_t reg, uint64_t imm);
    std::vector<uint8_t> generate_load_mem(uint8_t dst_reg, uint8_t base_reg, int32_t offset);
    std::vector<uint8_t> generate_store_mem(uint8_t src_reg, uint8_t base_reg, int32_t offset);
    std::vector<uint8_t> generate_jump(int32_t offset);
    std::vector<uint8_t> generate_conditional_jump(uint8_t condition, int32_t offset);
    std::vector<uint8_t> generate_call(uint64_t target);
    std::vector<uint8_t> generate_ret();
    
    // Optimization
    void optimize_instruction_sequence(std::vector<X64Instruction>& instructions);
    void peephole_optimize(std::vector<X64Instruction>& instructions);
    
private:
    void emit_rex_prefix(std::vector<uint8_t>& code, bool w, bool r, bool x, bool b);
    void emit_modrm(std::vector<uint8_t>& code, uint8_t mod, uint8_t reg, uint8_t rm);
    void emit_sib(std::vector<uint8_t>& code, uint8_t scale, uint8_t index, uint8_t base);
    void emit_immediate(std::vector<uint8_t>& code, uint64_t imm, uint8_t size);
};

// LLVM IR generation for advanced optimizations
#ifdef PSX5_ENABLE_LLVM
class LLVMCodeGenerator {
public:
    LLVMCodeGenerator();
    ~LLVMCodeGenerator();
    
    std::optional<JIT::CompileResult> compile_block(const JIT::InstructionBlock& block, 
                                                   JIT::RegisterAllocator& allocator);
    
private:
    void* llvm_context_;
    void* llvm_module_;
    void* llvm_builder_;
    void* llvm_engine_;
    
    void initialize_llvm();
    void cleanup_llvm();
};
#endif
