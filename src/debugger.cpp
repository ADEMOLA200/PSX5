#include "debugger.h"
#include "debugger/advanced_debugger.h"
#include "runtime/emulator.h"
#include "core/logger.h"
#include <iostream>
#include <sstream>

Debugger::Debugger(Emulator& emu) : emu_(emu) {
    emu_.attach_debugger(this);
    advanced_debugger_ = std::make_unique<PS5Emu::AdvancedDebugger>(emu_);
}

void Debugger::set_breakpoint(uint64_t pc){
    breaks_.insert(pc);
    advanced_debugger_->set_breakpoint(pc);
    log::info(std::string("breakpoint set @ ") + std::to_string(pc));
}

bool Debugger::handle_command(const std::string& line){
    std::istringstream ss(line);
    std::string cmd; ss >> cmd;
    
    if (advanced_debugger_->execute_script_command(line)) {
        return true;
    }
    
    if(cmd == "help"){
        std::cout<<"Basic commands: step, run, regs, mem <addr> <len>, break <addr>, cont, quit, help"<<std::endl;
        std::cout<<"Advanced commands: disasm <addr> <count>, profile <start|stop|report>, trace <start|stop>, watch <addr> <size>"<<std::endl;
        std::cout<<"                  symbols <file>, monitor <start|stop>, callstack, coverage <start> <end>"<<std::endl;
        return true;
    } else if(cmd == "step"){
        emu_.cpu().step();
        uint64_t pc = emu_.cpu().pc();
        std::cout<<"PC="<<pc<<"\n";
        
        advanced_debugger_->update_execution_flow(pc);
        return true;
    } else if(cmd == "run") {
        emu_.run_until_halt(1000000);
        std::cout<<"run finished PC="<<emu_.cpu().pc()<<"\n";
        return true;
    } else if(cmd == "cont") {
        while(emu_.cpu().running()){
            uint64_t pc = emu_.cpu().pc();
            
            if(breaks_.count(pc) || advanced_debugger_->is_breakpoint_hit(pc)) { 
                std::cout<<"hit breakpoint @"<<pc<<"\n"; 
                break; 
            }
            
            emu_.cpu().step();
            advanced_debugger_->update_execution_flow(pc);
        }
        return true;
    } else if(cmd == "regs") {
        auto regs = emu_.cpu().regs();
        for(int i=0;i<8;++i) std::cout<<"r"<<i<<"="<<regs[i]<<"\n";
        return true;
    } else if(cmd == "mem") {
        uint64_t addr; size_t len; if(!(ss>>addr>>len)){ std::cout<<"usage: mem <addr> <len>\n"; return true; }
        for(size_t i=0;i<len;i+=8){
            uint64_t v = emu_.memory().read64(static_cast<size_t>(addr+i));
            std::cout<<std::hex<<addr+i<<": "<<v<<std::dec<<"\n";
        }
        return true;
    } else if(cmd == "break") {
        uint64_t addr; if(!(ss>>addr)){ std::cout<<"usage: break <addr>\n"; return true; }
        set_breakpoint(addr);
        return true;
    } else if(cmd == "disasm") {
        uint64_t addr; uint32_t count = 10;
        if(!(ss>>addr)){ std::cout<<"usage: disasm <addr> [count]\n"; return true; }
        ss >> count;
        
        auto instructions = advanced_debugger_->disassemble_range(addr, addr + count * 16);
        for(const auto& instr : instructions) {
            std::cout << std::hex << instr.address << ": " << instr.mnemonic << " " << instr.operands;
            if(!instr.comment.empty()) std::cout << " ; " << instr.comment;
            std::cout << std::dec << "\n";
        }
        return true;
    } else if(cmd == "callstack") {
        auto stack = advanced_debugger_->get_call_stack();
        std::cout << "Call Stack:\n";
        for(const auto& frame : stack) {
            std::cout << "  " << std::hex << frame.return_address << ": " << frame.function_name << std::dec << "\n";
        }
        return true;
    } else if(cmd == "monitor") {
        std::string action; ss >> action;
        if(action == "start") {
            advanced_debugger_->start_monitoring();
            std::cout << "System monitoring started\n";
        } else if(action == "stop") {
            advanced_debugger_->stop_monitoring();
            std::cout << "System monitoring stopped\n";
        } else if(action == "stats") {
            auto stats = advanced_debugger_->get_system_stats();
            std::cout << "CPU Utilization: " << stats.cpu.utilization_percent << "%\n";
            std::cout << "Memory Used: " << stats.memory.total_allocated << " bytes\n";
            std::cout << "GPU Utilization: " << stats.gpu.utilization_percent << "%\n";
        }
        return true;
    } else if(cmd == "trace") {
        std::string action; ss >> action;
        if(action == "start") {
            uint32_t max_entries = 10000; ss >> max_entries;
            advanced_debugger_->start_tracing(max_entries);
            std::cout << "Execution tracing started\n";
        } else if(action == "stop") {
            advanced_debugger_->stop_tracing();
            std::cout << "Execution tracing stopped\n";
        }
        return true;
    } else if(cmd == "symbols") {
        std::string filepath; ss >> filepath;
        if(!filepath.empty()) {
            advanced_debugger_->load_symbols(filepath);
            std::cout << "Symbols loaded from " << filepath << "\n";
        }
        return true;
    } else if(cmd == "quit") {
        return false;
    }
    return true;
}

void Debugger::repl(){
    std::string line;
    std::cout<<"Debugger REPL. type 'help' for commands."<<std::endl;
    while(true){
        std::cout<<"> "; if(!std::getline(std::cin, line)) break;
        if(line.empty()) continue;
        bool ok = handle_command(line);
        if(!ok) break;
    }
}
