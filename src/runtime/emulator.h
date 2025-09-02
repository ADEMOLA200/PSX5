#pragma once
#include "core/memory.h"
#include "core/cpu.h"
#include "core/syscalls.h"
#include "core/scheduler.h"
#include "loader/module_loader.h"
#include "gpu/gpu.h"
#include "audio/audio.h"
#include <vector>

class Debugger;

class Emulator {
public:
    Emulator(size_t mem_size = (1ull<<20));
    bool load_module(const std::vector<uint8_t>& bytes, uint64_t base);
    void run_until_halt(size_t max_steps = 10'000'000);

    // Accessors for debugger integration and external tools
    CPU& cpu() { return cpu_; }
    Memory& memory() { return mem_; }
    GPU& gpu() { return gpu_; }
    Audio& audio() { return audio_; }

    // Attach a debugger (optional)
    void attach_debugger(Debugger* dbg);

private:
    Memory mem_;
    Syscalls sys_;
    CPU cpu_;
    Scheduler sched_;
    ModuleLoader loader_;
    GPU gpu_;
    Audio audio_;
    Debugger* debugger_{nullptr};
};
