#include "syscalls.h"
#include "memory.h"
#include "ps5_bios.h"
#include <iostream>
#include <ctime>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>

Syscalls::Syscalls(Memory* mem) : mem_(mem) {}

Syscalls::~Syscalls() {
    // Close all open file descriptors
    for (auto& [fd, file] : fds_) {
        if (file) {
            fclose(file);
        }
    }
}

static std::string read_string_from_mem(Memory* mem, uint64_t addr){
    std::string s; if(!mem) return s;
    for(size_t i=0;;++i){
        if(addr + i >= mem->size()) break;
        char c = (char)mem->read8(static_cast<size_t>(addr + i));
        if(c==0) break;
        s.push_back(c);
    }
    return s;
}

void Syscalls::handle(uint8_t code, uint64_t* regs, bool& running){
    if (ps5_bios_ && code >= 100) {
        // Route PS5-specific system calls to BIOS
        handle_ps5_syscall(code, regs, running);
        return;
    }
    
    switch(code){
        case PrintU64: std::cout << regs[0] << std::endl; break;
        case ExitCode: running = false; break;
        // open: regs[0]=ptr to filename, regs[1]=mode (0=r,1=w), returns fd in regs[0]
        case 3: {
            uint64_t name_ptr = regs[0]; int mode = (int)regs[1];
            std::string name = read_string_from_mem(mem_, name_ptr);
            const char* m = mode==1?"wb":"rb";
            FILE* f = fopen(name.c_str(), m);
            if(!f) { regs[0] = (uint64_t)-1; }
            else { int fd = next_fd_++; fds_[fd]=f; regs[0]=fd; }
        } break;
        // read: regs[0]=fd, regs[1]=buf_ptr, regs[2]=len -> returns bytes read
        case 4: {
            int fd = (int)regs[0]; uint64_t buf = regs[1]; size_t len = (size_t)regs[2];
            auto it = fds_.find(fd);
            if(it==fds_.end()){ regs[0]=0; break; }
            FILE* f = it->second; std::vector<uint8_t> tmp(len);
            size_t r = fread(tmp.data(),1,len,f);
            if(mem_) mem_->store(buf, tmp.data(), r);
            regs[0]=r;
        } break;
        // write: regs[0]=fd, regs[1]=buf_ptr, regs[2]=len -> returns bytes written
        case 5: {
            int fd = (int)regs[0]; uint64_t buf = regs[1]; size_t len = (size_t)regs[2];
            auto it = fds_.find(fd);
            if(it==fds_.end()){ regs[0]=0; break; }
            FILE* f = it->second; std::vector<uint8_t> tmp(len);
            if(mem_) mem_->load(buf, tmp.data(), len);
            size_t w = fwrite(tmp.data(),1,len,f);
            regs[0]=w;
        } break;
        case 6: {
            std::time_t t = std::time(nullptr);
            regs[0] = (uint64_t)t;
        } break;
        // close(fd)
        case 7: {
            int fd = (int)regs[0]; auto it = fds_.find(fd); if(it!=fds_.end()){ fclose(it->second); fds_.erase(it); regs[0]=0; } else regs[0]=(uint64_t)-1;
        } break;
        // lseek(fd, offset, whence)
        case 8: {
            int fd = (int)regs[0]; int64_t off = (int64_t)regs[1]; int wh = (int)regs[2]; auto it = fds_.find(fd); if(it==fds_.end()){ regs[0]=(uint64_t)-1; break;} FILE* f = it->second; int res = fseek(f, (long)off, wh); if(res==0) regs[0]=ftell(f); else regs[0]=(uint64_t)-1;
        } break;
        // stat syscall
        case 9: {
            uint64_t path_ptr = regs[0];
            uint64_t stat_buf_ptr = regs[1];
            
            std::string path = read_string_from_mem(mem_, path_ptr);
            if (path.empty()) {
                regs[0] = (uint64_t)-1;
                break;
            }
            
            struct stat file_stat;
            int result = stat(path.c_str(), &file_stat);
            
            if (result == 0 && mem_) {
                // Copy stat structure to emulated memory
                // PS5 stat structure layout (simplified)
                // TODO: Implement proper PS5 stat structure layout
                mem_->write64(stat_buf_ptr + 0, file_stat.st_dev);      // Device ID
                mem_->write64(stat_buf_ptr + 8, file_stat.st_ino);      // Inode number
                mem_->write32(stat_buf_ptr + 16, file_stat.st_mode);    // File mode
                mem_->write32(stat_buf_ptr + 20, file_stat.st_nlink);   // Number of links
                mem_->write32(stat_buf_ptr + 24, file_stat.st_uid);     // User ID
                mem_->write32(stat_buf_ptr + 28, file_stat.st_gid);     // Group ID
                mem_->write64(stat_buf_ptr + 32, file_stat.st_rdev);    // Device ID (if special)
                mem_->write64(stat_buf_ptr + 40, file_stat.st_size);    // File size
                mem_->write64(stat_buf_ptr + 48, file_stat.st_atime);   // Access time
                mem_->write64(stat_buf_ptr + 56, file_stat.st_mtime);   // Modification time
                mem_->write64(stat_buf_ptr + 64, file_stat.st_ctime);   // Status change time
                mem_->write64(stat_buf_ptr + 72, file_stat.st_blksize); // Block size
                mem_->write64(stat_buf_ptr + 80, file_stat.st_blocks);  // Number of blocks
                
                regs[0] = 0;
            } else {
                regs[0] = (uint64_t)-1;
            }
        } break;
        // execve syscall
        case 10: {
            uint64_t path_ptr = regs[0];
            uint64_t argv_ptr = regs[1];
            uint64_t envp_ptr = regs[2];
            
            std::string path = read_string_from_mem(mem_, path_ptr);
            if (path.empty()) {
                regs[0] = (uint64_t)-1;
                break;
            }
            
            std::vector<std::string> argv_strings;
            std::vector<char*> argv_ptrs;
            
            if (argv_ptr != 0 && mem_) {
                for (size_t i = 0; ; ++i) {
                    uint64_t arg_ptr = mem_->read64(argv_ptr + i * 8);
                    if (arg_ptr == 0) break;
                    
                    std::string arg = read_string_from_mem(mem_, arg_ptr);
                    argv_strings.push_back(arg);
                    argv_ptrs.push_back(const_cast<char*>(argv_strings.back().c_str()));
                }
            }
            argv_ptrs.push_back(nullptr);
            
            std::vector<std::string> envp_strings;
            std::vector<char*> envp_ptrs;
            
            if (envp_ptr != 0 && mem_) {
                for (size_t i = 0; ; ++i) {
                    uint64_t env_ptr = mem_->read64(envp_ptr + i * 8);
                    if (env_ptr == 0) break;
                    
                    std::string env = read_string_from_mem(mem_, env_ptr);
                    envp_strings.push_back(env);
                    envp_ptrs.push_back(const_cast<char*>(envp_strings.back().c_str()));
                }
            }
            envp_ptrs.push_back(nullptr);
            
            pid_t pid = fork();
            if (pid == 0) {
                execve(path.c_str(), argv_ptrs.data(), envp_ptrs.data());
                // If we get here, execve failed
                exit(127);
            } else if (pid > 0) {
                // Parent process - wait for child
                int status;
                waitpid(pid, &status, 0);
                regs[0] = WEXITSTATUS(status);
            } else {
                // Fork failed
                regs[0] = (uint64_t)-1;
            }
        } break;
        case 11: { regs[0]=getpid(); } break;
        // mkdir syscall
        case 12: {
            uint64_t path_ptr = regs[0];
            uint32_t mode = (uint32_t)regs[1];
            
            std::string path = read_string_from_mem(mem_, path_ptr);
            if (path.empty()) {
                regs[0] = (uint64_t)-1;
                break;
            }
            
            int result = mkdir(path.c_str(), mode);
            regs[0] = (result == 0) ? 0 : (uint64_t)-1;
        } break;
        // rmdir syscall
        case 13: {
            uint64_t path_ptr = regs[0];
            
            std::string path = read_string_from_mem(mem_, path_ptr);
            if (path.empty()) {
                regs[0] = (uint64_t)-1;
                break;
            }
            
            int result = rmdir(path.c_str());
            regs[0] = (result == 0) ? 0 : (uint64_t)-1;
        } break;
        // unlink syscall
        case 14: {
            uint64_t path_ptr = regs[0];
            
            std::string path = read_string_from_mem(mem_, path_ptr);
            if (path.empty()) {
                regs[0] = (uint64_t)-1;
                break;
            }
            
            int result = unlink(path.c_str());
            regs[0] = (result == 0) ? 0 : (uint64_t)-1;
        } break;
        // chmod syscall
        case 15: {
            uint64_t path_ptr = regs[0];
            uint32_t mode = (uint32_t)regs[1];
            
            std::string path = read_string_from_mem(mem_, path_ptr);
            if (path.empty()) {
                regs[0] = (uint64_t)-1;
                break;
            }
            
            int result = chmod(path.c_str(), mode);
            regs[0] = (result == 0) ? 0 : (uint64_t)-1;
        } break;
        // access syscall
        case 16: {
            uint64_t path_ptr = regs[0];
            uint32_t mode = (uint32_t)regs[1];
            
            std::string path = read_string_from_mem(mem_, path_ptr);
            if (path.empty()) {
                regs[0] = (uint64_t)-1;
                break;
            }
            
            int result = access(path.c_str(), mode);
            regs[0] = (result == 0) ? 0 : (uint64_t)-1;
        } break;
        // getcwd syscall
        case 17: {
            uint64_t buf_ptr = regs[0];
            size_t size = (size_t)regs[1];
            
            if (!mem_ || buf_ptr == 0) {
                regs[0] = (uint64_t)-1;
                break;
            }
            
            std::vector<char> cwd_buf(size);
            char* result = getcwd(cwd_buf.data(), size);
            
            if (result) {
                size_t len = strlen(result) + 1; // Include null terminator
                if (len <= size) {
                    mem_->store(buf_ptr, reinterpret_cast<uint8_t*>(result), len);
                    regs[0] = buf_ptr; // Return buffer pointer
                } else {
                    regs[0] = (uint64_t)-1; // Buffer too small
                }
            } else {
                regs[0] = (uint64_t)-1;
            }
        } break;
        // chdir syscall
        case 18: {
            uint64_t path_ptr = regs[0];
            
            std::string path = read_string_from_mem(mem_, path_ptr);
            if (path.empty()) {
                regs[0] = (uint64_t)-1;
                break;
            }
            
            int result = chdir(path.c_str());
            regs[0] = (result == 0) ? 0 : (uint64_t)-1;
        } break;
        default: break;
    }
}

void Syscalls::handle_ps5_syscall(uint64_t syscall_number, uint64_t* regs, bool& running) {
    if (ps5_bios_) {
        std::cout << "Syscalls: Routing PS5 system call " << syscall_number << " to BIOS" << std::endl;
        ps5_bios_->handle_system_call(syscall_number, regs);
    } else {
        std::cout << "Syscalls: PS5 BIOS not available for system call " << syscall_number << std::endl;
        regs[0] = static_cast<uint64_t>(-1); // Return error
    }
}
