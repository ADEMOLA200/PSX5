\
#include "elf64_loader.h"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>

#pragma pack(push,1)
struct Elf64_Ehdr { unsigned char e_ident[16]; uint16_t e_type; uint16_t e_machine; uint32_t e_version; uint64_t e_entry; uint64_t e_phoff; uint64_t e_shoff; uint32_t e_flags; uint16_t e_ehsize; uint16_t e_phentsize; uint16_t e_phnum; uint16_t e_shentsize; uint16_t e_shnum; uint16_t e_shstrndx; };
struct Elf64_Phdr { uint32_t p_type; uint32_t p_flags; uint64_t p_offset; uint64_t p_vaddr; uint64_t p_paddr; uint64_t p_filesz; uint64_t p_memsz; uint64_t p_align; };
struct Elf64_Shdr { uint32_t sh_name; uint32_t sh_type; uint64_t sh_flags; uint64_t sh_addr; uint64_t sh_offset; uint64_t sh_size; uint32_t sh_link; uint32_t sh_info; uint64_t sh_addralign; uint64_t sh_entsize; };
struct Elf64_Sym { uint32_t st_name; unsigned char st_info; unsigned char st_other; uint16_t st_shndx; uint64_t st_value; uint64_t st_size; };
struct Elf64_Rela { uint64_t r_offset; uint64_t r_info; int64_t r_addend; };
#pragma pack(pop)

static inline uint32_t ELF64_R_TYPE(uint64_t i){ return (uint32_t)(i & 0xffffffffull); }
static inline uint32_t ELF64_R_SYM(uint64_t i){ return (uint32_t)(i >> 32); }

constexpr uint32_t PT_LOAD = 1;
constexpr uint32_t SHT_RELA = 4;
constexpr uint32_t SHT_DYNSYM = 11;
constexpr uint32_t SHT_STRTAB = 3;

constexpr uint32_t R_X86_64_RELATIVE = 8;
constexpr uint32_t R_X86_64_GLOB_DAT = 6;
constexpr uint32_t R_X86_64_JUMP_SLOT = 7;
constexpr uint32_t R_X86_64_64 = 1;

std::optional<ElfModule> Elf64Loader::load_from_bytes(const std::vector<uint8_t>& bytes){
    if(bytes.size() < sizeof(Elf64_Ehdr)) return std::nullopt;
    Elf64_Ehdr hdr; std::memcpy(&hdr, bytes.data(), sizeof(hdr));
    if(!(hdr.e_ident[0]==0x7f && hdr.e_ident[1]=='E' && hdr.e_ident[2]=='L' && hdr.e_ident[3]=='F')) return std::nullopt;
    if(hdr.e_ident[4] != 2) return std::nullopt; // 64-bit
    if(hdr.e_ident[5] != 1) return std::nullopt; // little endian

    // Read program headers: map PT_LOAD
    uint64_t min_vaddr = UINT64_MAX, max_vaddr = 0;
    std::vector<Elf64_Phdr> phdrs;
    for(uint16_t i=0;i<hdr.e_phnum;++i){
        size_t off = hdr.e_phoff + i * hdr.e_phentsize;
        if(off + sizeof(Elf64_Phdr) > bytes.size()) return std::nullopt;
        Elf64_Phdr ph; std::memcpy(&ph, bytes.data()+off, sizeof(ph));
        phdrs.push_back(ph);
        if(ph.p_type == PT_LOAD){
            if(ph.p_vaddr < min_vaddr) min_vaddr = ph.p_vaddr;
            if(ph.p_vaddr + ph.p_memsz > max_vaddr) max_vaddr = ph.p_vaddr + ph.p_memsz;
        }
    }
    if(min_vaddr==UINT64_MAX) min_vaddr = 0;
    uint64_t base_vaddr = min_vaddr;
    uint64_t total_size = (max_vaddr > base_vaddr) ? (max_vaddr - base_vaddr) : 0;
    if(total_size == 0) return std::nullopt;
    if(total_size > (1ull<<40)) return std::nullopt;

    std::vector<uint8_t> blob(total_size, 0);
    // copy segments
    for(auto &ph: phdrs){
        if(ph.p_type != PT_LOAD) continue;
        if(ph.p_offset + ph.p_filesz > bytes.size()) return std::nullopt;
        uint64_t dst = ph.p_vaddr - base_vaddr;
        if(dst + ph.p_filesz > blob.size()) return std::nullopt;
        std::memcpy(blob.data()+dst, bytes.data()+ph.p_offset, (size_t)ph.p_filesz);
    }

    // Parse section headers to find .dynsym, .dynstr, .rela.dyn, .rela.plt
    std::vector<Elf64_Shdr> shdrs;
    for(uint16_t i=0;i<hdr.e_shnum;++i){
        size_t off = hdr.e_shoff + i * hdr.e_shentsize;
        if(off + sizeof(Elf64_Shdr) > bytes.size()) { shdrs.push_back(Elf64_Shdr{}); continue; }
        Elf64_Shdr sh; std::memcpy(&sh, bytes.data()+off, sizeof(sh));
        shdrs.push_back(sh);
    }
    // section header string table
    std::string shstr;
    if(hdr.e_shstrndx < shdrs.size()){
        auto &sh = shdrs[hdr.e_shstrndx];
        if(sh.sh_offset + sh.sh_size <= bytes.size()) shstr.assign((char*)bytes.data()+sh.sh_offset, sh.sh_size);
    }
    auto sh_name = [&](uint32_t idx)->std::string{
        if(idx >= shstr.size()) return std::string();
        return std::string(shstr.c_str() + idx);
    };

    int dynsym_idx=-1, dynstr_idx=-1, rela_dyn_idx=-1, rela_plt_idx=-1;
    for(size_t i=0;i<shdrs.size();++i){
        auto &sh = shdrs[i];
        std::string name = sh_name(sh.sh_name);
        if(name==".dynsym") dynsym_idx = (int)i;
        else if(name==".dynstr") dynstr_idx = (int)i;
        else if(name==".rela.dyn") rela_dyn_idx = (int)i;
        else if(name==".rela.plt") rela_plt_idx = (int)i;
    }

    std::vector<char> dynstr;
    if(dynstr_idx>=0){
        auto &sh = shdrs[dynstr_idx];
        if(sh.sh_offset + sh.sh_size <= bytes.size()) dynstr.insert(dynstr.end(), bytes.begin()+sh.sh_offset, bytes.begin()+sh.sh_offset+sh.sh_size);
    }
    std::vector<Elf64_Sym> dynsyms;
    if(dynsym_idx>=0){
        auto &sh = shdrs[dynsym_idx];
        size_t count = sh.sh_size / sizeof(Elf64_Sym);
        size_t off = sh.sh_offset;
        for(size_t i=0;i<count;++i){
            if(off + sizeof(Elf64_Sym) > bytes.size()) break;
            Elf64_Sym s; std::memcpy(&s, bytes.data()+off, sizeof(s));
            dynsyms.push_back(s); off += sizeof(Elf64_Sym);
        }
    }

    // Build symbol name list
    std::vector<std::string> sym_names;
    for(auto &s: dynsyms){
        std::string nm;
        if(s.st_name < dynstr.size()) nm = std::string(dynstr.data() + s.st_name);
        sym_names.push_back(nm);
    }

    // Create PLT/GOT stubs for undefined symbols
    // TODO: Implement PLT/GOT stub creation

// Map common libc symbol names to emulator Syscall codes (host-side mapping)
std::unordered_map<std::string, uint8_t> name_to_syscall = {
    {"write", 5},
    {"read", 4},
    {"open", 3},
    {"close", 7},
    {"lseek", 8},
    {"time", 6},
    {"getpid", 11},
    {"fstat", 9}
};
// For undefined symbols, when building PLT stubs, look up name in this map and choose syscall code if present.

    std::vector<size_t> undefined_indices;
    for(size_t i=0;i<dynsyms.size();++i) if(dynsyms[i].st_shndx == 0) undefined_indices.push_back(i);
    size_t plt_stub_len = 3; // SYSCALL sc; HALT
    uint64_t old_size = blob.size();
    for(size_t i=0;i<undefined_indices.size();++i){
        uint8_t sc = uint8_t(200 + (i & 0x3F));
        blob.push_back(0x10); blob.push_back(sc); blob.push_back(0xFF);
    }

    // Apply relocations: RELA entries
    auto apply_rela = [&](int shidx){
        if(shidx < 0) return;
        auto &sh = shdrs[shidx];
        size_t entries = sh.sh_size / sizeof(Elf64_Rela);
        size_t off = sh.sh_offset;
        for(size_t e=0;e<entries;++e){
            if(off + sizeof(Elf64_Rela) > bytes.size()) break;
            Elf64_Rela rela; std::memcpy(&rela, bytes.data()+off, sizeof(rela));
            uint32_t rtype = ELF64_R_TYPE(rela.r_info);
            uint32_t symidx = ELF64_R_SYM(rela.r_info);
            uint64_t write_addr = rela.r_offset - base_vaddr;
            if(write_addr + 8 <= blob.size()){
                if(rtype == R_X86_64_RELATIVE){
                    uint64_t val = (uint64_t)(base_vaddr + rela.r_addend);
                    std::memcpy(blob.data()+write_addr, &val, 8);
                } else if(rtype == R_X86_64_64 || rtype == R_X86_64_GLOB_DAT || rtype == R_X86_64_JUMP_SLOT){
                    if(symidx < dynsyms.size()){
                        if(dynsyms[symidx].st_shndx != 0){
                            // symbol defined in this module: resolve to its value relative to base
                            uint64_t target = dynsyms[symidx].st_value - base_vaddr + base_vaddr;
                            uint64_t val = target + rela.r_addend;
                            std::memcpy(blob.data()+write_addr, &val, 8);
                        } else {
                            // undefined symbol: patch to PLT stub address we appended
                            size_t which = 0;
                            for(size_t k=0;k<undefined_indices.size();++k) if(undefined_indices[k]==symidx){ which=k; break; }
                            uint64_t stub_addr = base_vaddr + old_size + which * plt_stub_len;
                            std::memcpy(blob.data()+write_addr, &stub_addr, 8);
                        }
                    }
                } else {
                    // other reloc types ignored for now
                }
            }
            off += sizeof(Elf64_Rela);
        }
    };
    apply_rela(rela_dyn_idx);
    apply_rela(rela_plt_idx);

    ElfModule m;
    m.entry = hdr.e_entry - base_vaddr;
    m.code = std::move(blob);
    m.load_address = base_vaddr;
    return m;
}
