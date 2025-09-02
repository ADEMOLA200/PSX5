#pragma once
#include <cstdint>
#include <optional>
#include <functional>
#include <vector>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <mutex>

class CPU;
class Memory;

struct TraceCompiled {
    std::function<void(CPU&)> fn;
    size_t bytes_consumed;
    uint32_t execution_count;
    uint64_t total_cycles;
    std::chrono::high_resolution_clock::time_point last_executed;
    bool is_hot_trace;
    std::vector<uint64_t> branch_targets;
    uint32_t optimization_level;
};

struct TraceProfile {
    uint64_t start_pc;
    uint32_t execution_count;
    uint64_t total_execution_time_ns;
    float average_ipc;
    bool contains_loops;
    bool contains_calls;
    std::vector<uint16_t> instruction_histogram;
    uint32_t cache_misses;
    uint32_t branch_mispredictions;
};

class DynarecBase {
public:
    enum class OptimizationLevel {
        NONE = 0,
        BASIC = 1,
        AGGRESSIVE = 2,
        MAXIMUM = 3
    };
    
    struct CompilationStats {
        uint64_t traces_compiled;
        uint64_t traces_executed;
        uint64_t total_compilation_time_us;
        uint64_t total_execution_time_us;
        uint64_t cache_hits;
        uint64_t cache_misses;
        float average_trace_length;
        float compilation_overhead_ratio;
    };
    
    DynarecBase();
    ~DynarecBase();
    
    // Core compilation interface
    std::optional<TraceCompiled> compile_trace(const uint8_t* mem, size_t pc, size_t max_bytes);
    std::optional<TraceCompiled> compile_x86_trace(const uint8_t* mem, size_t pc, size_t max_bytes);
    
    // Advanced compilation with profiling
    std::optional<TraceCompiled> compile_optimized_trace(const uint8_t* mem, size_t pc, 
                                                        size_t max_bytes, OptimizationLevel level);
    
    // Cache management
    void invalidate_cache();
    void invalidate_range(uint64_t start, uint64_t end);
    void flush_hot_traces();
    
    // Configuration
    void set_optimization_level(OptimizationLevel level) { optimization_level_ = level; }
    void enable_profiling(bool enable) { profiling_enabled_ = enable; }
    void set_max_trace_length(size_t length) { max_trace_length_ = length; }
    void set_hot_threshold(uint32_t threshold) { hot_threshold_ = threshold; }
    
    // Statistics and profiling
    const CompilationStats& get_stats() const { return stats_; }
    const std::unordered_map<uint64_t, TraceProfile>& get_profiles() const { return trace_profiles_; }
    std::vector<uint64_t> get_hot_traces(float threshold = 0.8f) const;
    void dump_performance_report(const std::string& filename) const;
    
private:
    OptimizationLevel optimization_level_;
    bool profiling_enabled_;
    size_t max_trace_length_;
    uint32_t hot_threshold_;
    
    // Trace cache and profiling
    std::unordered_map<uint64_t, TraceCompiled> trace_cache_;
    std::unordered_map<uint64_t, TraceProfile> trace_profiles_;
    CompilationStats stats_;
    std::mutex cache_mutex_;
    
    // Code generation backends
    std::optional<TraceCompiled> generate_asmjit_trace(const std::vector<X86Instruction>& instructions);
    std::optional<TraceCompiled> generate_interpreter_trace(const std::vector<X86Instruction>& instructions, 
                                                           uint64_t start_pc, size_t bytes_consumed);
    
    // Instruction analysis and optimization
    struct X86Instruction {
        uint64_t pc;
        uint16_t opcode;
        uint8_t modrm;
        uint8_t sib;
        uint8_t rex;
        uint8_t prefixes;
        int32_t displacement;
        uint64_t immediate;
        uint8_t length;
        bool reads_memory;
        bool writes_memory;
        bool is_branch;
        bool is_call;
        bool is_conditional;
        std::vector<uint8_t> registers_read;
        std::vector<uint8_t> registers_written;
    };
    
    std::vector<X86Instruction> decode_x86_sequence(const uint8_t* mem, size_t pc, size_t max_bytes);
    void optimize_instruction_sequence(std::vector<X86Instruction>& instructions);
    void apply_peephole_optimizations(std::vector<X86Instruction>& instructions);
    void eliminate_redundant_moves(std::vector<X86Instruction>& instructions);
    void optimize_memory_accesses(std::vector<X86Instruction>& instructions);
    void optimize_branch_sequences(std::vector<X86Instruction>& instructions);
    
    // Profiling and analysis
    void update_trace_profile(uint64_t pc, const TraceCompiled& trace, uint64_t execution_time_ns);
    bool is_hot_trace(uint64_t pc) const;
    void mark_trace_as_hot(uint64_t pc);
    
    // Utility functions
    bool is_block_terminator(uint16_t opcode) const;
    bool is_branch_instruction(uint16_t opcode) const;
    uint64_t estimate_instruction_cycles(const X86Instruction& instr) const;
    size_t calculate_trace_complexity(const std::vector<X86Instruction>& instructions) const;
};

// Legacy interface for compatibility
class Dynarec : public DynarecBase {
public:
    Dynarec() : DynarecBase() {}
    ~Dynarec() = default;
    
    // Legacy method - delegates to enhanced implementation
    std::optional<TraceCompiled> compile_trace(const uint8_t* mem, size_t pc, size_t max_bytes) {
        return DynarecBase::compile_trace(mem, pc, max_bytes);
    }
};
