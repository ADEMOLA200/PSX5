#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <cstdio>
#include <memory>

class Memory;
class PS5BIOS;

class Syscalls {
public:
    explicit Syscalls(Memory* mem = nullptr);
    ~Syscalls();
    
    void set_ps5_bios(std::shared_ptr<PS5BIOS> bios) { ps5_bios_ = bios; }
    
    enum : uint8_t {
        PrintU64 = 1,
        ExitCode = 2
    };
    
    void handle(uint8_t code, uint64_t* regs, bool& running);
    
    void handle_ps5_syscall(uint64_t syscall_number, uint64_t* regs, bool& running);
    
private:
    Memory* mem_{nullptr};
    std::unordered_map<int, FILE*> fds_;
    int next_fd_{3};
    
    std::shared_ptr<PS5BIOS> ps5_bios_;
};
