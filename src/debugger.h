#pragma once
#include <cstdint>
#include <string>
#include <set>
#include <memory>

class Emulator;

namespace PS5Emu {
    class AdvancedDebugger;
}

class Debugger {
public:
    Debugger(Emulator& emu);
    void repl();
    void set_breakpoint(uint64_t pc);
    
    PS5Emu::AdvancedDebugger* get_advanced_debugger() { return advanced_debugger_.get(); }
    
private:
    Emulator& emu_;
    std::set<uint64_t> breaks_;
    bool handle_command(const std::string& cmd);
    
    std::unique_ptr<PS5Emu::AdvancedDebugger> advanced_debugger_;
};
