#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <chrono>
#include <functional>
#include <thread>
#include <mutex>

class Emulator;
class Memory;
class CPU;

namespace PS5Emu {

struct Instruction {
    uint64_t address;
    std::vector<uint8_t> bytes;
    std::string mnemonic;
    std::string operands;
    std::string comment;
    bool is_branch;
    bool is_call;
    bool is_return;
    uint64_t target_address;
};

struct MemoryRegion {
    uint64_t start_address;
    uint64_t end_address;
    std::string name;
    uint32_t permissions; // Read/Write/Execute flags
    bool is_mapped;
    std::string module_name;
};

struct CallStackFrame {
    uint64_t return_address;
    uint64_t frame_pointer;
    uint64_t stack_pointer;
    std::string function_name;
    std::string module_name;
    std::unordered_map<std::string, uint64_t> local_variables;
};

struct Breakpoint {
    enum Type {
        EXECUTION,
        MEMORY_READ,
        MEMORY_WRITE,
        MEMORY_ACCESS,
        CONDITIONAL
    };
    
    uint64_t address;
    Type type;
    bool enabled;
    uint32_t hit_count;
    uint32_t ignore_count;
    std::string condition;
    std::string name;
    std::function<bool()> condition_func;
};

struct Watchpoint {
    uint64_t address;
    size_t size;
    bool on_read;
    bool on_write;
    bool enabled;
    uint64_t old_value;
    uint32_t hit_count;
    std::string name;
};

struct ProfileData {
    struct FunctionProfile {
        std::string name;
        uint64_t call_count;
        uint64_t total_cycles;
        uint64_t self_cycles;
        std::chrono::nanoseconds total_time;
        std::chrono::nanoseconds self_time;
        std::unordered_map<uint64_t, uint64_t> callers; // caller_addr -> call_count
    };
    
    std::unordered_map<uint64_t, FunctionProfile> functions;
    uint64_t total_instructions;
    uint64_t total_cycles;
    std::chrono::nanoseconds total_time;
    std::chrono::system_clock::time_point start_time;
};

struct SystemMonitor {
    struct CPUStats {
        double utilization_percent;
        uint64_t instructions_per_second;
        uint64_t cycles_per_second;
        uint32_t cache_hit_rate_l1;
        uint32_t cache_hit_rate_l2;
        uint32_t cache_hit_rate_l3;
    };
    
    struct MemoryStats {
        uint64_t total_allocated;
        uint64_t total_free;
        uint64_t peak_usage;
        uint32_t allocation_count;
        uint32_t fragmentation_percent;
        std::vector<std::pair<uint64_t, size_t>> large_allocations;
    };
    
    struct GPUStats {
        double utilization_percent;
        uint64_t memory_used;
        uint64_t memory_total;
        uint32_t draw_calls_per_frame;
        uint32_t triangles_per_frame;
        double frame_time_ms;
    };
    
    CPUStats cpu;
    MemoryStats memory;
    GPUStats gpu;
    std::chrono::system_clock::time_point last_update;
};

class AdvancedDebugger {
private:
    Emulator& emulator_;
    
    // Breakpoints and watchpoints
    std::unordered_map<uint64_t, Breakpoint> breakpoints_;
    std::unordered_map<uint64_t, Watchpoint> watchpoints_;
    uint32_t next_breakpoint_id_;
    
    // Disassembly and analysis
    std::unordered_map<uint64_t, Instruction> instruction_cache_;
    std::unordered_map<uint64_t, std::string> symbol_table_;
    std::vector<MemoryRegion> memory_regions_;
    
    // Call stack and execution tracking
    std::vector<CallStackFrame> call_stack_;
    std::unordered_set<uint64_t> function_entries_;
    uint64_t current_function_start_;
    
    // Profiling
    ProfileData profile_data_;
    bool profiling_enabled_;
    std::chrono::high_resolution_clock::time_point last_profile_time_;
    
    // System monitoring
    SystemMonitor system_monitor_;
    std::thread monitor_thread_;
    bool monitoring_enabled_;
    std::mutex monitor_mutex_;
    
    // Scripting support
    std::unordered_map<std::string, std::function<void(const std::vector<std::string>&)>> script_commands_;
    
    // Logging and tracing
    struct TraceEntry {
        uint64_t address;
        std::string instruction;
        std::unordered_map<std::string, uint64_t> register_values;
        std::chrono::high_resolution_clock::time_point timestamp;
    };
    std::vector<TraceEntry> execution_trace_;
    bool tracing_enabled_;
    size_t max_trace_entries_;
    
    // Internal methods
    Instruction disassemble_instruction(uint64_t address);
    void update_call_stack(uint64_t pc);
    void update_profiling_data(uint64_t pc);
    void monitor_system_stats();
    bool evaluate_condition(const std::string& condition);
    void load_symbol_file(const std::string& filepath);
    std::string get_symbol_name(uint64_t address);
    
public:
    AdvancedDebugger(Emulator& emulator);
    ~AdvancedDebugger();
    
    // Breakpoint management
    uint32_t set_breakpoint(uint64_t address, Breakpoint::Type type = Breakpoint::EXECUTION);
    uint32_t set_conditional_breakpoint(uint64_t address, const std::string& condition);
    bool remove_breakpoint(uint32_t id);
    bool enable_breakpoint(uint32_t id, bool enabled);
    std::vector<Breakpoint> get_breakpoints();
    bool is_breakpoint_hit(uint64_t address);
    
    // Watchpoint management
    uint32_t set_watchpoint(uint64_t address, size_t size, bool on_read, bool on_write);
    bool remove_watchpoint(uint32_t id);
    bool enable_watchpoint(uint32_t id, bool enabled);
    std::vector<Watchpoint> get_watchpoints();
    bool check_watchpoints(uint64_t address, size_t size, bool is_write);
    
    // Disassembly and analysis
    std::vector<Instruction> disassemble_range(uint64_t start, uint64_t end);
    Instruction disassemble_at(uint64_t address);
    std::string analyze_function(uint64_t start_address);
    std::vector<uint64_t> find_function_calls(uint64_t function_address);
    std::vector<uint64_t> find_cross_references(uint64_t address);
    
    // Memory analysis
    std::vector<MemoryRegion> get_memory_regions();
    std::string analyze_memory_region(uint64_t address);
    std::vector<uint8_t> dump_memory(uint64_t address, size_t size);
    bool search_memory(const std::vector<uint8_t>& pattern, std::vector<uint64_t>& results);
    std::string get_memory_protection(uint64_t address);
    
    // Call stack and execution flow
    std::vector<CallStackFrame> get_call_stack();
    void update_execution_flow(uint64_t pc);
    std::string get_current_function();
    std::unordered_map<std::string, uint64_t> get_local_variables();
    
    // Profiling
    void start_profiling();
    void stop_profiling();
    ProfileData get_profile_data();
    void reset_profile_data();
    std::vector<std::pair<std::string, uint64_t>> get_hottest_functions(uint32_t count = 10);
    
    // System monitoring
    void start_monitoring();
    void stop_monitoring();
    SystemMonitor get_system_stats();
    std::string generate_performance_report();
    
    // Symbol management
    void load_symbols(const std::string& filepath);
    void add_symbol(uint64_t address, const std::string& name);
    std::string resolve_symbol(uint64_t address);
    std::vector<std::pair<uint64_t, std::string>> search_symbols(const std::string& pattern);
    
    // Execution tracing
    void start_tracing(size_t max_entries = 10000);
    void stop_tracing();
    std::vector<TraceEntry> get_execution_trace();
    void save_trace_to_file(const std::string& filepath);
    
    // Scripting interface
    void register_script_command(const std::string& name, std::function<void(const std::vector<std::string>&)> handler);
    bool execute_script_command(const std::string& command);
    void load_script_file(const std::string& filepath);
    
    // Advanced analysis
    struct CodeCoverage {
        std::unordered_set<uint64_t> executed_addresses;
        std::unordered_map<uint64_t, uint32_t> execution_counts;
        double coverage_percentage;
    };
    
    CodeCoverage analyze_code_coverage(uint64_t start, uint64_t end);
    std::vector<uint64_t> find_unreachable_code(uint64_t start, uint64_t end);
    std::string detect_code_patterns();
    
    // Interactive debugging
    void step_into();
    void step_over();
    void step_out();
    void run_to_cursor(uint64_t address);
    void run_until_return();
    
    // State management
    struct DebuggerState {
        std::unordered_map<std::string, uint64_t> registers;
        std::vector<uint8_t> memory_snapshot;
        std::vector<CallStackFrame> call_stack;
        uint64_t pc;
        std::chrono::system_clock::time_point timestamp;
    };
    
    void save_state(const std::string& name);
    bool restore_state(const std::string& name);
    std::vector<std::string> list_saved_states();
    
    // Event callbacks
    void on_breakpoint_hit(uint64_t address);
    void on_watchpoint_hit(uint64_t address, bool is_write);
    void on_exception(uint32_t exception_code, uint64_t address);
    void on_system_call(uint64_t syscall_number);
};

} // namespace PS5Emu
