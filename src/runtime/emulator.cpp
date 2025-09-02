#include "runtime/emulator.h"
#include "core/logger.h"
#include "debugger.h"

Emulator::Emulator(size_t mem_size)
    : mem_(mem_size), sys_(&mem_), cpu_(mem_, sys_), sched_(), loader_(), gpu_(), audio_() {
    // Invalidate JIT/code cache on writes to memory pages (simple heuristic)
    // TODO: Implement more sophisticated cache invalidation
    mem_.set_write_observer([this](size_t addr, size_t len){ this->cpu_.invalidate_code_at(addr, len); });
}


bool Emulator::load_module(const std::vector<uint8_t>& bytes, uint64_t base){
    auto mod = loader_.from_bytes(bytes);
    if(!mod) return false;
    if(!mem_.store(base, mod->code.data(), mod->code.size())) return false;
    cpu_.reset(base + mod->entry);
    return true;
}

void Emulator::run_until_halt(size_t max_steps){
    size_t steps = 0;
    while(cpu_.running() && steps < max_steps){
        cpu_.step();
        ++steps;
        if(debugger_ && !cpu_.running()) break; // allow debugger to inspect halted state
    }
    if(steps >= max_steps) log::warn("max step budget reached");
}

void Emulator::attach_debugger(Debugger* dbg){
    debugger_ = dbg;
    log::info("Debugger attached to emulator");
}
