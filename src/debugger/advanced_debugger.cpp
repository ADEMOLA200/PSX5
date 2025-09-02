#include "advanced_debugger.h"
#include "../runtime/emulator.h"
#include "../core/logger.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex>
#include <capstone/capstone.h>

namespace PS5Emu {

AdvancedDebugger::AdvancedDebugger(Emulator& emulator) 
    : emulator_(emulator), next_breakpoint_id_(1), profiling_enabled_(false),
      monitoring_enabled_(false), tracing_enabled_(false), max_trace_entries_(10000) {
    
    // Initialize Capstone disassembler for x86-64
    if (cs_open(CS_ARCH_X86, CS_MODE_64, &cs_handle_) != CS_ERR_OK) {
        log::error("Failed to initialize Capstone disassembler");
    }
    
    // Set up default script commands
    register_script_command("break", [this](const std::vector<std::string>& args) {
        if (args.size() >= 2) {
            uint64_t addr = std::stoull(args[1], nullptr, 0);
            set_breakpoint(addr);
            log::info("Breakpoint set at 0x" + std::to_string(addr));
        }
    });
    
    register_script_command("watch", [this](const std::vector<std::string>& args) {
        if (args.size() >= 3) {
            uint64_t addr = std::stoull(args[1], nullptr, 0);
            size_t size = std::stoull(args[2], nullptr, 0);
            set_watchpoint(addr, size, true, true);
            log::info("Watchpoint set at 0x" + std::to_string(addr));
        }
    });
    
    register_script_command("profile", [this](const std::vector<std::string>& args) {
        if (args.size() >= 2) {
            if (args[1] == "start") {
                start_profiling();
            } else if (args[1] == "stop") {
                stop_profiling();
            } else if (args[1] == "report") {
                auto hottest = get_hottest_functions(10);
                for (const auto& [name, cycles] : hottest) {
                    log::info("Function: " + name + " - Cycles: " + std::to_string(cycles));
                }
            }
        }
    });
    
    log::info("Advanced debugger initialized");
}

AdvancedDebugger::~AdvancedDebugger() {
    stop_monitoring();
    stop_profiling();
    stop_tracing();
    
    if (cs_handle_) {
        cs_close(&cs_handle_);
    }
}

uint32_t AdvancedDebugger::set_breakpoint(uint64_t address, Breakpoint::Type type) {
    Breakpoint bp;
    bp.address = address;
    bp.type = type;
    bp.enabled = true;
    bp.hit_count = 0;
    bp.ignore_count = 0;
    bp.name = "BP_" + std::to_string(next_breakpoint_id_);
    
    uint32_t id = next_breakpoint_id_++;
    breakpoints_[id] = bp;
    
    log::info("Breakpoint " + std::to_string(id) + " set at 0x" + 
              std::to_string(address) + " (type: " + std::to_string(static_cast<int>(type)) + ")");
    
    return id;
}

uint32_t AdvancedDebugger::set_conditional_breakpoint(uint64_t address, const std::string& condition) {
    uint32_t id = set_breakpoint(address, Breakpoint::CONDITIONAL);
    breakpoints_[id].condition = condition;
    
    // Compile condition into a lambda function
    breakpoints_[id].condition_func = [this, condition]() -> bool {
        return evaluate_condition(condition);
    };
    
    return id;
}

bool AdvancedDebugger::is_breakpoint_hit(uint64_t address) {
    for (auto& [id, bp] : breakpoints_) {
        if (bp.address == address && bp.enabled) {
            if (bp.ignore_count > 0) {
                bp.ignore_count--;
                continue;
            }
            
            // Check condition for conditional breakpoints
            if (bp.type == Breakpoint::CONDITIONAL) {
                if (!bp.condition_func || !bp.condition_func()) {
                    continue;
                }
            }
            
            bp.hit_count++;
            on_breakpoint_hit(address);
            return true;
        }
    }
    return false;
}

uint32_t AdvancedDebugger::set_watchpoint(uint64_t address, size_t size, bool on_read, bool on_write) {
    Watchpoint wp;
    wp.address = address;
    wp.size = size;
    wp.on_read = on_read;
    wp.on_write = on_write;
    wp.enabled = true;
    wp.hit_count = 0;
    wp.name = "WP_" + std::to_string(next_breakpoint_id_);
    
    // Read current value for comparison
    if (size <= 8) {
        wp.old_value = emulator_.memory().read64(address);
    }
    
    uint32_t id = next_breakpoint_id_++;
    watchpoints_[id] = wp;
    
    log::info("Watchpoint " + std::to_string(id) + " set at 0x" + 
              std::to_string(address) + " (size: " + std::to_string(size) + ")");
    
    return id;
}

bool AdvancedDebugger::check_watchpoints(uint64_t address, size_t size, bool is_write) {
    for (auto& [id, wp] : watchpoints_) {
        if (!wp.enabled) continue;
        
        // Check if access overlaps with watchpoint
        if (address < wp.address + wp.size && address + size > wp.address) {
            if ((is_write && wp.on_write) || (!is_write && wp.on_read)) {
                wp.hit_count++;
                on_watchpoint_hit(address, is_write);
                return true;
            }
        }
    }
    return false;
}

Instruction AdvancedDebugger::disassemble_instruction(uint64_t address) {
    // Check cache first
    auto it = instruction_cache_.find(address);
    if (it != instruction_cache_.end()) {
        return it->second;
    }
    
    Instruction instr;
    instr.address = address;
    
    // Read instruction bytes from memory
    std::vector<uint8_t> bytes(16); // Max x86-64 instruction length
    for (size_t i = 0; i < 16; i++) {
        bytes[i] = emulator_.memory().read8(address + i);
    }
    
    // Disassemble using Capstone
    cs_insn* insn;
    size_t count = cs_disasm(cs_handle_, bytes.data(), bytes.size(), address, 1, &insn);
    
    if (count > 0) {
        instr.bytes.assign(insn[0].bytes, insn[0].bytes + insn[0].size);
        instr.mnemonic = insn[0].mnemonic;
        instr.operands = insn[0].op_str;
        
        // Analyze instruction type
        instr.is_branch = (insn[0].id >= X86_INS_JA && insn[0].id <= X86_INS_JS) || 
                         (insn[0].id == X86_INS_JMP);
        instr.is_call = (insn[0].id == X86_INS_CALL);
        instr.is_return = (insn[0].id == X86_INS_RET);
        
        // Extract target address for branches/calls
        if (instr.is_branch || instr.is_call) {
            cs_x86& x86 = insn[0].detail->x86;
            for (int i = 0; i < x86.op_count; i++) {
                if (x86.operands[i].type == X86_OP_IMM) {
                    instr.target_address = x86.operands[i].imm;
                    break;
                }
            }
        }
        
        std::string symbol = get_symbol_name(address);
        if (!symbol.empty()) {
            instr.comment = symbol;
        }
        
        cs_free(insn, count);
    } else {
        instr.mnemonic = "db";
        instr.operands = "0x" + std::to_string(bytes[0]);
        instr.bytes = {bytes[0]};
    }
    
    instruction_cache_[address] = instr;
    return instr;
}

std::vector<Instruction> AdvancedDebugger::disassemble_range(uint64_t start, uint64_t end) {
    std::vector<Instruction> instructions;
    
    uint64_t current = start;
    while (current < end) {
        Instruction instr = disassemble_instruction(current);
        instructions.push_back(instr);
        
        current += std::max(static_cast<size_t>(1), instr.bytes.size());
    }
    
    return instructions;
}

void AdvancedDebugger::start_profiling() {
    if (profiling_enabled_) return;
    
    profiling_enabled_ = true;
    profile_data_ = ProfileData{};
    profile_data_.start_time = std::chrono::system_clock::now();
    last_profile_time_ = std::chrono::high_resolution_clock::now();
    
    log::info("Profiling started");
}

void AdvancedDebugger::stop_profiling() {
    if (!profiling_enabled_) return;
    
    profiling_enabled_ = false;
    profile_data_.total_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now() - last_profile_time_);
    
    log::info("Profiling stopped - Total time: " + 
              std::to_string(profile_data_.total_time.count()) + " ns");
}

void AdvancedDebugger::update_profiling_data(uint64_t pc) {
    if (!profiling_enabled_) return;
    
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_profile_time_);
    
    std::string function_name = get_symbol_name(pc);
    if (function_name.empty()) {
        function_name = "func_" + std::to_string(pc);
    }
    
    auto& func_profile = profile_data_.functions[pc];
    func_profile.name = function_name;
    func_profile.call_count++;
    func_profile.total_time += elapsed;
    func_profile.self_time += elapsed;
    
    profile_data_.total_instructions++;
    last_profile_time_ = now;
}

void AdvancedDebugger::start_monitoring() {
    if (monitoring_enabled_) return;
    
    monitoring_enabled_ = true;
    monitor_thread_ = std::thread(&AdvancedDebugger::monitor_system_stats, this);
    
    log::info("System monitoring started");
}

void AdvancedDebugger::stop_monitoring() {
    if (!monitoring_enabled_) return;
    
    monitoring_enabled_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    log::info("System monitoring stopped");
}

void AdvancedDebugger::monitor_system_stats() {
    while (monitoring_enabled_) {
        std::lock_guard<std::mutex> lock(monitor_mutex_);
        
        // Update CPU stats
        system_monitor_.cpu.instructions_per_second = profile_data_.total_instructions;
        system_monitor_.cpu.utilization_percent = 85.0; // Simulated value
        
        // Update memory stats
        system_monitor_.memory.total_allocated = emulator_.memory().get_allocated_size();
        system_monitor_.memory.total_free = emulator_.memory().get_free_size();
        
        // Update GPU stats (if available)
        system_monitor_.gpu.utilization_percent = 70.0; // Simulated value
        system_monitor_.gpu.memory_used = 8ULL * 1024 * 1024 * 1024; // 8GB
        system_monitor_.gpu.memory_total = 16ULL * 1024 * 1024 * 1024; // 16GB
        
        system_monitor_.last_update = std::chrono::system_clock::now();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

std::vector<std::pair<std::string, uint64_t>> AdvancedDebugger::get_hottest_functions(uint32_t count) {
    std::vector<std::pair<std::string, uint64_t>> hottest;
    
    for (const auto& [addr, profile] : profile_data_.functions) {
        hottest.emplace_back(profile.name, profile.total_cycles);
    }
    
    std::sort(hottest.begin(), hottest.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    if (hottest.size() > count) {
        hottest.resize(count);
    }
    
    return hottest;
}

void AdvancedDebugger::start_tracing(size_t max_entries) {
    tracing_enabled_ = true;
    max_trace_entries_ = max_entries;
    execution_trace_.clear();
    execution_trace_.reserve(max_entries);
    
    log::info("Execution tracing started (max entries: " + std::to_string(max_entries) + ")");
}

void AdvancedDebugger::stop_tracing() {
    tracing_enabled_ = false;
    log::info("Execution tracing stopped (" + std::to_string(execution_trace_.size()) + " entries)");
}

bool AdvancedDebugger::evaluate_condition(const std::string& condition) {
    // TODO: Implement proper and more sophisticated condition evaluation
    std::regex reg_pattern(R"((\w+)\s*([<>=!]+)\s*(\w+))");
    std::smatch matches;
    
    if (std::regex_match(condition, matches, reg_pattern)) {
        std::string left = matches[1].str();
        std::string op = matches[2].str();
        std::string right = matches[3].str();
        
        // Get register values
        auto regs = emulator_.cpu().regs();
        uint64_t left_val = 0, right_val = 0;
        
        // Parse left operand
        if (left == "rax") left_val = regs[0];
        else if (left == "rbx") left_val = regs[1];
        else if (left == "rcx") left_val = regs[2];
        else if (left == "rdx") left_val = regs[3];
        else left_val = std::stoull(left, nullptr, 0);
        
        // Parse right operand
        if (right == "rax") right_val = regs[0];
        else if (right == "rbx") right_val = regs[1];
        else if (right == "rcx") right_val = regs[2];
        else if (right == "rdx") right_val = regs[3];
        else right_val = std::stoull(right, nullptr, 0);
        
        // Evaluate condition
        if (op == "==") return left_val == right_val;
        else if (op == "!=") return left_val != right_val;
        else if (op == "<") return left_val < right_val;
        else if (op == ">") return left_val > right_val;
        else if (op == "<=") return left_val <= right_val;
        else if (op == ">=") return left_val >= right_val;
    }
    
    return false;
}

std::string AdvancedDebugger::get_symbol_name(uint64_t address) {
    auto it = symbol_table_.find(address);
    if (it != symbol_table_.end()) {
        return it->second;
    }
    return "";
}

void AdvancedDebugger::load_symbols(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        log::error("Failed to open symbol file: " + filepath);
        return;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string addr_str, symbol_name;
        
        if (iss >> addr_str >> symbol_name) {
            uint64_t address = std::stoull(addr_str, nullptr, 16);
            symbol_table_[address] = symbol_name;
        }
    }
    
    log::info("Loaded " + std::to_string(symbol_table_.size()) + " symbols from " + filepath);
}

void AdvancedDebugger::register_script_command(const std::string& name, 
                                               std::function<void(const std::vector<std::string>&)> handler) {
    script_commands_[name] = handler;
}

bool AdvancedDebugger::execute_script_command(const std::string& command) {
    std::istringstream iss(command);
    std::vector<std::string> args;
    std::string arg;
    
    while (iss >> arg) {
        args.push_back(arg);
    }
    
    if (args.empty()) return false;
    
    auto it = script_commands_.find(args[0]);
    if (it != script_commands_.end()) {
        it->second(args);
        return true;
    }
    
    return false;
}

void AdvancedDebugger::on_breakpoint_hit(uint64_t address) {
    log::info("Breakpoint hit at 0x" + std::to_string(address));
    
    // Update call stack
    update_call_stack(address);
    
    // Show context
    auto instructions = disassemble_range(address - 32, address + 32);
    for (const auto& instr : instructions) {
        std::string marker = (instr.address == address) ? ">>> " : "    ";
        log::info(marker + std::to_string(instr.address) + ": " + 
                 instr.mnemonic + " " + instr.operands);
    }
}

void AdvancedDebugger::on_watchpoint_hit(uint64_t address, bool is_write) {
    std::string access_type = is_write ? "write" : "read";
    log::info("Watchpoint hit at 0x" + std::to_string(address) + " (" + access_type + ")");
}

void AdvancedDebugger::update_call_stack(uint64_t pc) {
    // Simple call stack reconstruction
    auto instr = disassemble_instruction(pc);
    
    if (instr.is_call) {
        CallStackFrame frame;
        frame.return_address = pc + instr.bytes.size();
        frame.frame_pointer = emulator_.cpu().regs()[5]; // RBP
        frame.stack_pointer = emulator_.cpu().regs()[4]; // RSP
        frame.function_name = get_symbol_name(instr.target_address);
        
        call_stack_.push_back(frame);
    } else if (instr.is_return && !call_stack_.empty()) {
        call_stack_.pop_back();
    }
}

}
