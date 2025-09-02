#include <iostream>
#include <vector>
#include <cassert>
#include "../src/core/memory.h"
#include "../src/core/syscalls.h"
#include "../src/core/cpu.h"

// Simple assertion helper
static int tests_run = 0;
static int tests_failed = 0;
#define EXPECT_EQ(a,b) do { ++tests_run; if((a)!=(b)){ std::cerr<<"Test failed: Expected "<<(b)<<" got "<<(a)<<" at "<<__FILE__<<":"<<__LINE__<<"\n"; ++tests_failed; }} while(0)

// Build a program helper
static std::vector<uint8_t> build_prog() {
    std::vector<uint8_t> p;
    auto push64 = [&](uint64_t v){ for(int i=0;i<8;++i) p.push_back(uint8_t(v >> (8*i))); };
    // LOAD_IMM r0, 10
    p.push_back(0x01); p.push_back(0x00); push64(10);
    // ADD_IMM r0, 5
    p.push_back(0x02); p.push_back(0x00); push64(5);
    // MUL_IMM r0, 3
    p.push_back(0x04); p.push_back(0x00); push64(3);
    // STORE_MEM r0, addr 0x200
    p.push_back(0x07); p.push_back(0x00); push64(0x200);
    // LOAD_MEM r1, addr 0x200
    p.push_back(0x06); p.push_back(0x01); push64(0x200);
    // CMP_IMM r1, 45  ; zero_flag true if equal
    p.push_back(0x08); p.push_back(0x01); push64(45);
    // JZ forward by +9 bytes (skips next LOAD_IMM if equal)
    p.push_back(0x0A); p.push_back(0); p.push_back(0); p.push_back(9); p.push_back(0);
    // LOAD_IMM r2, 999  ; should be skipped
    p.push_back(0x01); p.push_back(0x02); push64(999);
    // LOAD_IMM r2, 123  ; executed if JZ taken
    p.push_back(0x01); p.push_back(0x02); push64(123);
    // SYSCALL PrintU64 (prints r0)
    p.push_back(0x10); p.push_back(0x01);
    // HALT
    p.push_back(0xFF);
    return p;
}

int main(){
    Memory mem(1<<16);
    Syscalls sc;
    CPU cpu(mem, sc);
    auto prog = build_prog();
    // load at base 0x1000
    bool ok = mem.store(0x1000, prog.data(), prog.size());
    if(!ok){ std::cerr<<"Failed to store program"<<std::endl; return 2; }
    cpu.reset(0x1000);
    cpu.run_steps(1000);

    // After execution, r0 should be (10+5)*3 = 45
    EXPECT_EQ(cpu.regs()[0], 45ull);
    // r1 should be loaded from memory (45)
    EXPECT_EQ(cpu.regs()[1], 45ull);
    // r2 should be 123 (because JZ should be taken)
    EXPECT_EQ(cpu.regs()[2], 123ull);

    if(tests_failed==0){
        std::cout<<"All tests passed ("<<tests_run<<")"<<std::endl;
        return 0;
    } else {
        std::cerr<<tests_failed<<" tests failed out of "<<tests_run<<std::endl;
        return 1;
    }
}
