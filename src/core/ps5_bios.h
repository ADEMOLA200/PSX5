#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>

class Memory;
class CPU;

// PS5 BIOS and Kernel Interface Emulation
// Implements Sony's custom FreeBSD-based kernel and system services
// TODO: Implement user-space to kernel-space context switching

class PS5BIOS {
public:
    explicit PS5BIOS(Memory& memory, CPU& cpu);
    ~PS5BIOS();
    
    // BIOS initialization and boot process
    bool initialize();
    void boot_sequence();
    void load_system_modules();
    
    // Kernel services
    void handle_system_call(uint64_t syscall_number, uint64_t* registers);
    void handle_interrupt(uint8_t vector);
    
    // Memory management
    uint64_t allocate_virtual_memory(uint64_t size, uint32_t protection);
    void free_virtual_memory(uint64_t address, uint64_t size);
    bool map_physical_memory(uint64_t virtual_addr, uint64_t physical_addr, uint64_t size);
    
    // Process management
    struct PS5Process {
        uint32_t pid;
        uint64_t base_address;
        uint64_t entry_point;
        uint64_t stack_base;
        uint64_t heap_base;
        std::string name;
        bool is_system_process;
        uint32_t privilege_level;
    };
    
    uint32_t create_process(const std::string& name, uint64_t entry_point);
    void terminate_process(uint32_t pid);
    PS5Process* get_process(uint32_t pid);
    
    // File system interface
    struct PS5FileSystem {
        std::unordered_map<std::string, uint64_t> mounted_devices;
        std::unordered_map<int, std::string> open_files;
        int next_fd = 3;
    } filesystem;
    
    // Security and encryption
    struct SecurityModule {
        bool secure_boot_enabled;
        std::vector<uint8_t> master_key;
        std::vector<uint8_t> per_console_key;
        bool hypervisor_active;
    } security;
    
    // System configuration
    struct SystemConfig {
        uint64_t total_memory;
        uint32_t cpu_frequency;
        uint32_t gpu_frequency;
        std::string firmware_version;
        bool dev_mode_enabled;
    } config;
    
private:
    Memory& memory_;
    CPU& cpu_;
    
    // System tables and structures
    std::vector<uint64_t> interrupt_descriptor_table_;
    std::vector<uint64_t> global_descriptor_table_;
    std::unordered_map<uint32_t, PS5Process> processes_;
    uint32_t next_pid_ = 1;
    
    // Boot ROM and system modules
    std::vector<uint8_t> boot_rom_;
    std::vector<uint8_t> kernel_image_;
    std::vector<uint8_t> hypervisor_image_;
    
    // Internal functions
    void setup_memory_layout();
    void initialize_interrupt_handlers();
    void load_boot_rom();
    void start_hypervisor();
    void initialize_security_module();
    
    // System call handlers
    void sys_exit(uint64_t* regs);
    void sys_fork(uint64_t* regs);
    void sys_read(uint64_t* regs);
    void sys_write(uint64_t* regs);
    void sys_open(uint64_t* regs);
    void sys_close(uint64_t* regs);
    void sys_mmap(uint64_t* regs);
    void sys_munmap(uint64_t* regs);
    void sys_getpid(uint64_t* regs);
    void sys_socket(uint64_t* regs);
    void sys_thread_create(uint64_t* regs);
    void sys_mutex_create(uint64_t* regs);
    void sys_gpu_submit(uint64_t* regs);
    void sys_audio_output(uint64_t* regs);
    
    // Trophy system
    void sys_trophy_unlock(uint64_t* regs);
    void sys_trophy_progress(uint64_t* regs);
    void sys_trophy_query(uint64_t* regs);
    void sys_trophy_list(uint64_t* regs);
    
    // PSN system calls
    void sys_psn_authenticate(uint64_t* regs);
    void sys_psn_get_friends(uint64_t* regs);
    void sys_psn_send_message(uint64_t* regs);
    void sys_psn_create_session(uint64_t* regs);
    void sys_psn_join_session(uint64_t* regs);
    void sys_psn_sync_trophies(uint64_t* regs);
    void sys_psn_upload_save(uint64_t* regs);
    void sys_psn_download_save(uint64_t* regs);
};

// PS5 Kernel Interface - FreeBSD-based system calls
enum PS5SystemCall : uint64_t {
    // Process management
    SYS_EXIT = 1,
    SYS_FORK = 2,
    SYS_READ = 3,
    SYS_WRITE = 4,
    SYS_OPEN = 5,
    SYS_CLOSE = 6,
    SYS_WAIT4 = 7,
    SYS_CREAT = 8,
    SYS_LINK = 9,
    SYS_UNLINK = 10,
    SYS_EXECVE = 11,
    SYS_CHDIR = 12,
    SYS_FCHDIR = 13,
    SYS_MKNOD = 14,
    SYS_CHMOD = 15,
    SYS_CHOWN = 16,
    SYS_GETPID = 20,
    
    // Memory management
    SYS_MMAP = 477,
    SYS_MUNMAP = 73,
    SYS_MPROTECT = 74,
    SYS_MADVISE = 75,
    SYS_MINCORE = 78,
    
    // Threading
    SYS_THR_CREATE = 430,
    SYS_THR_EXIT = 431,
    SYS_THR_SELF = 432,
    SYS_THR_KILL = 433,
    SYS_THR_SUSPEND = 434,
    SYS_THR_WAKE = 435,
    
    // Synchronization
    SYS_MUTEX_CREATE = 500,
    SYS_MUTEX_DESTROY = 501,
    SYS_MUTEX_LOCK = 502,
    SYS_MUTEX_UNLOCK = 503,
    SYS_COND_CREATE = 504,
    SYS_COND_DESTROY = 505,
    SYS_COND_WAIT = 506,
    SYS_COND_SIGNAL = 507,
    
    // PS5 specific system calls
    SYS_PS5_GPU_SUBMIT = 600,
    SYS_PS5_GPU_MEMORY_ALLOC = 601,
    SYS_PS5_GPU_MEMORY_FREE = 602,
    SYS_PS5_AUDIO_OUTPUT = 610,
    SYS_PS5_AUDIO_INPUT = 611,
    SYS_PS5_CONTROLLER_READ = 620,
    SYS_PS5_STORAGE_READ = 630,
    SYS_PS5_STORAGE_WRITE = 631,
    SYS_PS5_NETWORK_SOCKET = 640,
    SYS_PS5_SECURITY_DECRYPT = 650,
    SYS_PS5_SECURITY_ENCRYPT = 651,
    
    // Trophy system
    SYS_PS5_TROPHY_UNLOCK = 700,
    SYS_PS5_TROPHY_PROGRESS = 701,
    SYS_PS5_TROPHY_QUERY = 702,
    SYS_PS5_TROPHY_LIST = 703,
    SYS_PS5_TROPHY_ICON = 704,
    
    // PSN Integration
    SYS_PS5_PSN_AUTHENTICATE = 800,
    SYS_PS5_PSN_GET_FRIENDS = 801,
    SYS_PS5_PSN_SEND_MESSAGE = 802,
    SYS_PS5_PSN_CREATE_SESSION = 803,
    SYS_PS5_PSN_JOIN_SESSION = 804,
    SYS_PS5_PSN_SYNC_TROPHIES = 805,
    SYS_PS5_PSN_UPLOAD_SAVE = 806,
    SYS_PS5_PSN_DOWNLOAD_SAVE = 807,
    SYS_PS5_PSN_GET_STORE = 808,
    SYS_PS5_PSN_PURCHASE = 809,
};

// PS5 Memory Layout Constants
namespace PS5MemoryLayout {
    constexpr uint64_t KERNEL_BASE = 0xFFFFFFFF80000000ULL;
    constexpr uint64_t KERNEL_SIZE = 0x2000000ULL; // 32MB kernel
    
    constexpr uint64_t USER_BASE = 0x0000000000400000ULL;
    constexpr uint64_t USER_SIZE = 0x7FFFFFFC00000ULL; // ~128TB user space
    
    constexpr uint64_t GPU_MEMORY_BASE = 0x0000000100000000ULL;
    constexpr uint64_t GPU_MEMORY_SIZE = 0x0000000400000000ULL; // 16GB GPU memory
    
    constexpr uint64_t HYPERVISOR_BASE = 0xFFFFFFFFF0000000ULL;
    constexpr uint64_t HYPERVISOR_SIZE = 0x10000000ULL; // 256MB hypervisor
    
    constexpr uint64_t BOOT_ROM_BASE = 0xFFFFFFFFFFFF0000ULL;
    constexpr uint64_t BOOT_ROM_SIZE = 0x10000ULL; // 64KB boot ROM
}
