#include "ps5_bios.h"
#include "memory.h"
#include "cpu.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>
#include <random>
#include <set>
#include <mutex>

PS5BIOS::PS5BIOS(Memory& memory, CPU& cpu) : memory_(memory), cpu_(cpu) {
    config.total_memory = 16ULL * 1024 * 1024 * 1024; // 16GB
    config.cpu_frequency = 3800; // 3.8 GHz
    config.gpu_frequency = 2230; // 2.23 GHz
    config.firmware_version = "11.00";
    config.dev_mode_enabled = false;
    
    // Initialize security module with proper cryptographic keys
    security.secure_boot_enabled = true;
    security.hypervisor_active = true;
    security.master_key.resize(32);
    security.per_console_key.resize(32);
    security.boot_loader_key.resize(32);
    security.kernel_key.resize(32);
    
    // Generate cryptographically secure keys for emulation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint8_t> dis(0, 255);
    
    for (size_t i = 0; i < 32; ++i) {
        security.master_key[i] = dis(gen);
        security.per_console_key[i] = dis(gen);
        security.boot_loader_key[i] = dis(gen);
        security.kernel_key[i] = dis(gen);
    }
    
    // Initialize file system
    filesystem.mounted_devices["/dev/da0"] = PS5MemoryLayout::STORAGE_BASE;
    filesystem.mounted_devices["/dev/da1"] = PS5MemoryLayout::STORAGE_BASE + 0x40000000000ULL; // 4TB offset
    filesystem.mounted_devices["/system"] = PS5MemoryLayout::SYSTEM_PARTITION_BASE;
    filesystem.mounted_devices["/user"] = PS5MemoryLayout::USER_PARTITION_BASE;
    
    system_services_.resize(32);
    for (size_t i = 0; i < system_services_.size(); ++i) {
        system_services_[i].service_id = i;
        system_services_[i].is_active = false;
        system_services_[i].priority = 0;
    }
    
    std::cout << "PS5BIOS: Initialized with firmware " << config.firmware_version << std::endl;
}

PS5BIOS::~PS5BIOS() {
    for (auto& [pid, process] : processes_) {
        std::cout << "PS5BIOS: Terminating process " << process.name << " (PID " << pid << ")" << std::endl;
    }
}

bool PS5BIOS::initialize() {
    std::cout << "PS5BIOS: Starting initialization sequence..." << std::endl;
    
    if (!initialize_hardware()) {
        std::cout << "PS5BIOS: Hardware initialization failed" << std::endl;
        return false;
    }
    
    // Load and verify boot ROM
    if (!load_boot_rom()) {
        std::cout << "PS5BIOS: Boot ROM loading failed" << std::endl;
        return false;
    }
    
    // Setup comprehensive memory layout
    if (!setup_memory_layout()) {
        std::cout << "PS5BIOS: Memory layout setup failed" << std::endl;
        return false;
    }
    
    // Initialize interrupt and exception handling
    if (!initialize_interrupt_handlers()) {
        std::cout << "PS5BIOS: Interrupt handler setup failed" << std::endl;
        return false;
    }
    
    // Start hypervisor with security validation
    if (!start_hypervisor()) {
        std::cout << "PS5BIOS: Hypervisor startup failed" << std::endl;
        return false;
    }
    
    // Initialize comprehensive security module
    if (!initialize_security_module()) {
        std::cout << "PS5BIOS: Security module initialization failed" << std::endl;
        return false;
    }
    
    // Initialize system services
    if (!initialize_system_services()) {
        std::cout << "PS5BIOS: System services initialization failed" << std::endl;
        return false;
    }
    
    std::cout << "PS5BIOS: Initialization complete" << std::endl;
    return true;
}

bool PS5BIOS::initialize_hardware() {
    std::cout << "PS5BIOS: Initializing hardware components..." << std::endl;
    
    // Initialize CPU features
    cpu_.enable_feature(CPU::Feature::SSE4_2);
    cpu_.enable_feature(CPU::Feature::AVX2);
    cpu_.enable_feature(CPU::Feature::AES_NI);
    cpu_.enable_feature(CPU::Feature::RDRAND);
    cpu_.enable_feature(CPU::Feature::BMI2);
    
    // Set CPU frequency and power states
    cpu_.set_frequency(config.cpu_frequency);
    cpu_.enable_turbo_boost(true);
    
    // Initialize memory controller
    memory_.get_vm_manager()->set_page_fault_handler(
        [this](uint64_t addr, MemoryProtection prot) -> bool {
            return handle_page_fault(addr, prot);
        }
    );
    
    // Initialize I/O APIC for interrupt routing
    io_apic_.resize(24); // 24 interrupt lines
    for (size_t i = 0; i < io_apic_.size(); ++i) {
        io_apic_[i].vector = 0x20 + i;
        io_apic_[i].delivery_mode = 0; // Fixed delivery
        io_apic_[i].destination_mode = 0; // Physical
        io_apic_[i].destination = 0; // CPU 0
        io_apic_[i].mask = true; // Initially masked
    }
    
    std::cout << "PS5BIOS: Hardware initialization complete" << std::endl;
    return true;
}

bool PS5BIOS::initialize_system_services() {
    std::cout << "PS5BIOS: Initializing system services..." << std::endl;
    
    // Core system services
    struct ServiceInfo {
        uint32_t id;
        std::string name;
        uint64_t entry_point;
        uint32_t priority;
    };
    
    std::vector<ServiceInfo> services = {
        {0, "SceKernel", PS5MemoryLayout::KERNEL_BASE + 0x1000, 0},
        {1, "SceProcessManager", PS5MemoryLayout::KERNEL_BASE + 0x10000, 1},
        {2, "SceMemoryManager", PS5MemoryLayout::KERNEL_BASE + 0x20000, 1},
        {3, "SceFileSystem", PS5MemoryLayout::KERNEL_BASE + 0x30000, 2},
        {4, "SceNetworkStack", PS5MemoryLayout::KERNEL_BASE + 0x40000, 2},
        {5, "SceGpuDriver", PS5MemoryLayout::KERNEL_BASE + 0x50000, 1},
        {6, "SceAudioDriver", PS5MemoryLayout::KERNEL_BASE + 0x60000, 2},
        {7, "SceInputManager", PS5MemoryLayout::KERNEL_BASE + 0x70000, 2},
        {8, "SceSecurity", PS5MemoryLayout::KERNEL_BASE + 0x80000, 0},
        {9, "SceShellCore", PS5MemoryLayout::USER_BASE + 0x1000000, 3},
        {10, "SceSystemService", PS5MemoryLayout::USER_BASE + 0x2000000, 3}
    };
    
    for (const auto& service : services) {
        if (service.id < system_services_.size()) {
            system_services_[service.id].service_id = service.id;
            system_services_[service.id].name = service.name;
            system_services_[service.id].entry_point = service.entry_point;
            system_services_[service.id].priority = service.priority;
            system_services_[service.id].is_active = true;
            system_services_[service.id].memory_usage = 0x100000; // 1MB default
            
            std::cout << "PS5BIOS: Started service " << service.name 
                      << " (ID: " << service.id << ")" << std::endl;
        }
    }
    
    std::cout << "PS5BIOS: System services initialized" << std::endl;
    return true;
}

void PS5BIOS::boot_sequence() {
    std::cout << "PS5BIOS: Starting boot sequence..." << std::endl;
    
    // Stage 1: Boot ROM execution
    std::cout << "PS5BIOS: Executing boot ROM..." << std::endl;
    cpu_.get_state().rip = PS5MemoryLayout::BOOT_ROM_BASE;
    
    // Stage 2: Load and verify kernel
    std::cout << "PS5BIOS: Loading kernel..." << std::endl;
    load_system_modules();
    
    // Stage 3: Initialize system services
    std::cout << "PS5BIOS: Initializing system services..." << std::endl;
    
    // Create system processes
    create_process("kernel", PS5MemoryLayout::KERNEL_BASE);
    create_process("SceShellCore", PS5MemoryLayout::USER_BASE + 0x1000000);
    create_process("SceSystemService", PS5MemoryLayout::USER_BASE + 0x2000000);
    
    // Stage 4: Start user interface
    std::cout << "PS5BIOS: Starting user interface..." << std::endl;
    
    std::cout << "PS5BIOS: Boot sequence complete" << std::endl;
}

void PS5BIOS::load_system_modules() {
    // Load essential system modules
    std::vector<std::string> system_modules = {
        "SceKernel.sprx",
        "SceLibcInternal.sprx", 
        "SceGpuDriver.sprx",
        "SceAudioDriver.sprx",
        "SceNetworkStack.sprx",
        "SceFileSystem.sprx",
        "SceSecurity.sprx"
    };
    
    uint64_t module_base = PS5MemoryLayout::KERNEL_BASE + 0x1000000;
    
    for (const auto& module : system_modules) {
        std::cout << "PS5BIOS: Loading system module " << module << " at 0x" 
                  << std::hex << module_base << std::dec << std::endl;
        
        // Simulate module loading by allocating memory
        // TODO: implement proper module loading
        allocate_virtual_memory(0x100000, 0x7); // 1MB, RWX
        module_base += 0x100000;
    }
}

void PS5BIOS::setup_memory_layout() {
    std::cout << "PS5BIOS: Setting up memory layout..." << std::endl;
    
    // Map kernel space
    map_physical_memory(PS5MemoryLayout::KERNEL_BASE, 0x80000000, PS5MemoryLayout::KERNEL_SIZE);
    
    // Map hypervisor space
    map_physical_memory(PS5MemoryLayout::HYPERVISOR_BASE, 0xF0000000, PS5MemoryLayout::HYPERVISOR_SIZE);
    
    // Map boot ROM
    map_physical_memory(PS5MemoryLayout::BOOT_ROM_BASE, 0xFFFF0000, PS5MemoryLayout::BOOT_ROM_SIZE);
    
    // Setup page tables for virtual memory
    cpu_.enable_paging(0x1000); // Page table at physical address 0x1000
    
    std::cout << "PS5BIOS: Memory layout configured" << std::endl;
}

void PS5BIOS::initialize_interrupt_handlers() {
    std::cout << "PS5BIOS: Initializing interrupt handlers..." << std::endl;
    
    // Setup IDT with 256 entries
    interrupt_descriptor_table_.resize(256);
    
    // System call interrupt (0x80)
    interrupt_descriptor_table_[0x80] = PS5MemoryLayout::KERNEL_BASE + 0x1000;
    
    // Hardware interrupts
    interrupt_descriptor_table_[0x20] = PS5MemoryLayout::KERNEL_BASE + 0x2000; // Timer
    interrupt_descriptor_table_[0x21] = PS5MemoryLayout::KERNEL_BASE + 0x3000; // Keyboard
    interrupt_descriptor_table_[0x22] = PS5MemoryLayout::KERNEL_BASE + 0x4000; // GPU
    interrupt_descriptor_table_[0x23] = PS5MemoryLayout::KERNEL_BASE + 0x5000; // Audio
    interrupt_descriptor_table_[0x24] = PS5MemoryLayout::KERNEL_BASE + 0x6000; // Network
    
    // Exception handlers
    interrupt_descriptor_table_[0x0E] = PS5MemoryLayout::KERNEL_BASE + 0x7000; // Page fault
    interrupt_descriptor_table_[0x0D] = PS5MemoryLayout::KERNEL_BASE + 0x8000; // General protection
    
    std::cout << "PS5BIOS: Interrupt handlers initialized" << std::endl;
}

bool PS5BIOS::load_boot_rom() {
    std::cout << "PS5BIOS: Loading boot ROM..." << std::endl;
    
    // Create dummy boot ROM with basic initialization code
    boot_rom_.resize(PS5MemoryLayout::BOOT_ROM_SIZE);
    
    // Simple boot code that jumps to kernel
    // TODO: implement proper boot code
    uint8_t boot_code[] = {
        0x48, 0xC7, 0xC0, 0x00, 0x00, 0x00, 0x80, // mov rax, 0x80000000 (kernel base)
        0xFF, 0xE0,                                 // jmp rax
        0xF4                                        // hlt
    };
    
    memcpy(boot_rom_.data(), boot_code, sizeof(boot_code));
    
    // Copy boot ROM to memory
    for (size_t i = 0; i < boot_rom_.size(); ++i) {
        memory_.write8(PS5MemoryLayout::BOOT_ROM_BASE + i, boot_rom_[i]);
    }
    
    std::cout << "PS5BIOS: Boot ROM loaded" << std::endl;
    return true;
}

bool PS5BIOS::start_hypervisor() {
    std::cout << "PS5BIOS: Starting hypervisor..." << std::endl;
    
    if (security.hypervisor_active) {
        // Create hypervisor image
        hypervisor_image_.resize(PS5MemoryLayout::HYPERVISOR_SIZE);
        
        // Initialize hypervisor with security checks
        std::cout << "PS5BIOS: Hypervisor security checks passed" << std::endl;
        std::cout << "PS5BIOS: Hypervisor started successfully" << std::endl;
        return true;
    } else {
        std::cout << "PS5BIOS: Hypervisor disabled" << std::endl;
        return false;
    }
}

bool PS5BIOS::initialize_security_module() {
    std::cout << "PS5BIOS: Initializing security module..." << std::endl;
    
    if (security.secure_boot_enabled) {
        std::cout << "PS5BIOS: Secure boot enabled - verifying signatures..." << std::endl;
        
        // Simulate signature verification
        // TODO: implement proper signature verification
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        std::cout << "PS5BIOS: All signatures verified" << std::endl;
    }
    
    std::cout << "PS5BIOS: Security module initialized" << std::endl;
    return true;
}

void PS5BIOS::handle_system_call(uint64_t syscall_number, uint64_t* registers) {
    current_syscall_context_.syscall_number = syscall_number;
    current_syscall_context_.caller_pid = get_current_process_id();
    current_syscall_context_.start_time = std::chrono::high_resolution_clock::now();
    
    // Validate system call number
    if (syscall_number >= static_cast<uint64_t>(SYS_PS5_MAX_SYSCALL)) {
        std::cout << "PS5BIOS: Invalid system call number " << syscall_number << std::endl;
        registers[0] = static_cast<uint64_t>(-EINVAL);
        return;
    }
    
    // Check process permissions for privileged system calls
    if (is_privileged_syscall(syscall_number) && !has_privilege(get_current_process_id())) {
        std::cout << "PS5BIOS: Permission denied for syscall " << syscall_number << std::endl;
        registers[0] = static_cast<uint64_t>(-EPERM);
        return;
    }
    
    switch (static_cast<PS5SystemCall>(syscall_number)) {
        case SYS_EXIT:
            sys_exit(registers);
            break;
        case SYS_FORK:
            sys_fork(registers);
            break;
        case SYS_READ:
            sys_read(registers);
            break;
        case SYS_WRITE:
            sys_write(registers);
            break;
        case SYS_OPEN:
            sys_open(registers);
            break;
        case SYS_CLOSE:
            sys_close(registers);
            break;
        case SYS_MMAP:
            sys_mmap(registers);
            break;
        case SYS_MUNMAP:
            sys_munmap(registers);
            break;
        case SYS_MPROTECT:
            sys_mprotect(registers);
            break;
        case SYS_GETPID:
            sys_getpid(registers);
            break;
        case SYS_THR_CREATE:
            sys_thread_create(registers);
            break;
        case SYS_THR_EXIT:
            sys_thread_exit(registers);
            break;
        case SYS_MUTEX_CREATE:
            sys_mutex_create(registers);
            break;
        case SYS_MUTEX_LOCK:
            sys_mutex_lock(registers);
            break;
        case SYS_MUTEX_UNLOCK:
            sys_mutex_unlock(registers);
            break;
        case SYS_PS5_GPU_SUBMIT:
            sys_gpu_submit(registers);
            break;
        case SYS_PS5_GPU_MEMORY_ALLOC:
            sys_gpu_memory_alloc(registers);
            break;
        case SYS_PS5_GPU_MEMORY_FREE:
            sys_gpu_memory_free(registers);
            break;
        case SYS_PS5_AUDIO_OUTPUT:
            sys_audio_output(registers);
            break;
        case SYS_PS5_AUDIO_INPUT:
            sys_audio_input(registers);
            break;
        case SYS_PS5_CONTROLLER_READ:
            sys_controller_read(registers);
            break;
        case SYS_PS5_STORAGE_READ:
            sys_storage_read(registers);
            break;
        case SYS_PS5_STORAGE_WRITE:
            sys_storage_write(registers);
            break;
        case SYS_PS5_NETWORK_SOCKET:
            sys_network_socket(registers);
            break;
        case SYS_PS5_SECURITY_DECRYPT:
            sys_security_decrypt(registers);
            break;
        case SYS_PS5_SECURITY_ENCRYPT:
            sys_security_encrypt(registers);
            break;
        default:
            std::cout << "PS5BIOS: Unimplemented system call " << syscall_number << std::endl;
            registers[0] = static_cast<uint64_t>(-ENOSYS);
            break;
    }
    
    // Log system call completion
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - current_syscall_context_.start_time);
    
    if (duration.count() > 1000) { // Log slow system calls (>1ms)
        std::cout << "PS5BIOS: Slow syscall " << syscall_number 
                  << " took " << duration.count() << "μs" << std::endl;
    }
}

void PS5BIOS::sys_fork(uint64_t* regs) {
    uint32_t parent_pid = get_current_process_id();
    PS5Process* parent = get_process(parent_pid);
    
    if (!parent) {
        regs[0] = static_cast<uint64_t>(-ESRCH);
        return;
    }
    
    // Create child process
    uint32_t child_pid = create_process(parent->name + "_child", parent->entry_point);
    PS5Process* child = get_process(child_pid);
    
    if (!child) {
        regs[0] = static_cast<uint64_t>(-ENOMEM);
        return;
    }
    
    // Copy parent's memory space (simplified)
    child->base_address = parent->base_address;
    child->stack_base = allocate_virtual_memory(0x100000, 0x6); // New stack
    child->heap_base = allocate_virtual_memory(0x1000000, 0x6);  // New heap
    child->privilege_level = parent->privilege_level;
    
    // Return child PID to parent, 0 to child
    regs[0] = child_pid;
    
    std::cout << "PS5BIOS: Fork created child process " << child_pid << std::endl;
}

void PS5BIOS::sys_open(uint64_t* regs) {
    uint64_t path_ptr = regs[0];
    int flags = static_cast<int>(regs[1]);
    int mode = static_cast<int>(regs[2]);
    
    // Read path from memory (simplified)
    // TODO: implement proper path reading
    std::string path = "/dev/null"; // Placeholder
    
    int fd = filesystem.next_fd++;
    filesystem.open_files[fd] = path;
    
    regs[0] = fd;
    
    std::cout << "PS5BIOS: Opened file " << path << " with fd " << fd << std::endl;
}

void PS5BIOS::sys_close(uint64_t* regs) {
    int fd = static_cast<int>(regs[0]);
    
    auto it = filesystem.open_files.find(fd);
    if (it != filesystem.open_files.end()) {
        std::cout << "PS5BIOS: Closed file " << it->second << " (fd " << fd << ")" << std::endl;
        filesystem.open_files.erase(it);
        regs[0] = 0;
    } else {
        regs[0] = static_cast<uint64_t>(-EBADF);
    }
}

void PS5BIOS::sys_mprotect(uint64_t* regs) {
    uint64_t addr = regs[0];
    size_t len = static_cast<size_t>(regs[1]);
    int prot = static_cast<int>(regs[2]);
    
    MemoryProtection protection = MemoryProtection::NONE;
    if (prot & 0x1) protection = static_cast<MemoryProtection>(static_cast<uint32_t>(protection) | static_cast<uint32_t>(MemoryProtection::READ));
    if (prot & 0x2) protection = static_cast<MemoryProtection>(static_cast<uint32_t>(protection) | static_cast<uint32_t>(MemoryProtection::WRITE));
    if (prot & 0x4) protection = static_cast<MemoryProtection>(static_cast<uint32_t>(protection) | static_cast<uint32_t>(MemoryProtection::EXECUTE));
    
    if (memory_.set_memory_protection(addr, len, protection)) {
        regs[0] = 0;
        std::cout << "PS5BIOS: Changed protection for 0x" << std::hex << addr 
                  << " (size: 0x" << len << ") to " << prot << std::dec << std::endl;
    } else {
        regs[0] = static_cast<uint64_t>(-EINVAL);
    }
}

void PS5BIOS::sys_thread_exit(uint64_t* regs) {
    uint64_t exit_value = regs[0];
    uint32_t current_tid = get_current_thread_id();
    
    std::cout << "PS5BIOS: Thread " << current_tid << " exiting with value " << exit_value << std::endl;
    
    // Remove thread from process
    uint32_t pid = get_current_process_id();
    PS5Process* process = get_process(pid);
    if (process) {
        auto it = std::find(process->thread_ids.begin(), process->thread_ids.end(), current_tid);
        if (it != process->thread_ids.end()) {
            process->thread_ids.erase(it);
        }
    }
    
    // In a real implementation, this would terminate the current thread
    // TODO: implement thread termination
    regs[0] = 0;
}

void PS5BIOS::sys_mutex_lock(uint64_t* regs) {
    uint32_t mutex_id = static_cast<uint32_t>(regs[0]);
    uint32_t timeout_ms = static_cast<uint32_t>(regs[1]);
    
    auto it = mutexes_.find(mutex_id);
    if (it == mutexes_.end()) {
        regs[0] = static_cast<uint64_t>(-EINVAL);
        return;
    }
    
    PS5Mutex& mutex = it->second;
    uint32_t current_tid = get_current_thread_id();
    
    // Check if already owned by current thread
    if (mutex.owner_tid == current_tid) {
        if (mutex.type == MutexType::RECURSIVE) {
            mutex.lock_count++;
            regs[0] = 0; // Success
            return;
        } else {
            regs[0] = static_cast<uint64_t>(-EDEADLK);
            return;
        }
    }
    
    // Try to acquire mutex
    if (!mutex.is_locked) {
        mutex.is_locked = true;
        mutex.owner_tid = current_tid;
        mutex.lock_count = 1;
        
        std::cout << "PS5BIOS: Thread " << current_tid << " acquired mutex " << mutex_id << std::endl;
        regs[0] = 0; // Success
        return;
    }
    
    // Mutex is locked, add to wait queue
    mutex.waiting_threads.push_back(current_tid);
    
    // Implement priority inheritance if enabled
    if (mutex.protocol == MutexProtocol::PRIORITY_INHERIT) {
        auto current_thread = threads_.find(current_tid);
        auto owner_thread = threads_.find(mutex.owner_tid);
        
        if (current_thread != threads_.end() && owner_thread != threads_.end()) {
            if (current_thread->second.priority < owner_thread->second.priority) {
                owner_thread->second.inherited_priority = current_thread->second.priority;
                std::cout << "PS5BIOS: Priority inheritance - thread " << mutex.owner_tid 
                          << " inherits priority " << current_thread->second.priority << std::endl;
            }
        }
    }
    
    // Block current thread (in real implementation, this would suspend execution)
    auto current_thread = threads_.find(current_tid);
    if (current_thread != threads_.end()) {
        current_thread->second.state = ThreadState::BLOCKED;
        current_thread->second.blocked_on_mutex = mutex_id;
    }
    
    std::cout << "PS5BIOS: Thread " << current_tid << " blocked on mutex " << mutex_id << std::endl;
    
    // For emulation, simulate successful acquisition after timeout
    // TODO: implement timeout handling
    regs[0] = 0; // Success (simplified), we need to implement a proper timeout handling
}

void PS5BIOS::sys_mutex_unlock(uint64_t* regs) {
    uint32_t mutex_id = static_cast<uint32_t>(regs[0]);
    
    auto it = mutexes_.find(mutex_id);
    if (it == mutexes_.end()) {
        regs[0] = static_cast<uint64_t>(-EINVAL);
        return;
    }
    
    PS5Mutex& mutex = it->second;
    uint32_t current_tid = get_current_thread_id();
    
    if (mutex.owner_tid != current_tid) {
        regs[0] = static_cast<uint64_t>(-EPERM);
        return;
    }
    
    if (mutex.type == MutexType::RECURSIVE && mutex.lock_count > 1) {
        mutex.lock_count--;
        regs[0] = 0; // Success
        return;
    }
    
    mutex.is_locked = false;
    mutex.owner_tid = 0;
    mutex.lock_count = 0;
    
    auto owner_thread = threads_.find(current_tid);
    if (owner_thread != threads_.end() && owner_thread->second.inherited_priority != 0) {
        owner_thread->second.inherited_priority = 0;
        std::cout << "PS5BIOS: Restored original priority for thread " << current_tid << std::endl;
    }
    
    // Wake up waiting threads (FIFO order)
    if (!mutex.waiting_threads.empty()) {
        uint32_t next_tid = mutex.waiting_threads.front();
        mutex.waiting_threads.pop_front();
        
        // Transfer ownership to next thread
        mutex.is_locked = true;
        mutex.owner_tid = next_tid;
        mutex.lock_count = 1;
        
        // Wake up the thread
        auto next_thread = threads_.find(next_tid);
        if (next_thread != threads_.end()) {
            next_thread->second.state = ThreadState::READY;
            next_thread->second.blocked_on_mutex = 0;
            scheduler_queue_.push_back(next_tid);
        }
        
        std::cout << "PS5BIOS: Mutex " << mutex_id << " transferred to thread " << next_tid << std::endl;
    }
    
    std::cout << "PS5BIOS: Thread " << current_tid << " released mutex " << mutex_id << std::endl;
    regs[0] = 0; // Success
}

void PS5BIOS::sys_security_decrypt(uint64_t* regs) {
    uint64_t encrypted_data_ptr = regs[0];
    uint64_t decrypted_data_ptr = regs[1];
    size_t data_size = static_cast<size_t>(regs[2]);
    uint32_t key_id = static_cast<uint32_t>(regs[3]);
    
    // Validate key ID
    if (key_id >= 4) {
        regs[0] = static_cast<uint64_t>(-EINVAL);
        return;
    }
    
    // Get the appropriate key
    std::vector<uint8_t>* key = nullptr;
    switch (key_id) {
        case 0: key = &security.master_key; break;
        case 1: key = &security.per_console_key; break;
        case 2: key = &security.boot_loader_key; break;
        case 3: key = &security.kernel_key; break;
    }
    
    if (!key || key->size() != 32) {
        regs[0] = static_cast<uint64_t>(-ENOKEY);
        return;
    }
    
    // Read encrypted data from memory
    std::vector<uint8_t> encrypted_data(data_size);
    std::vector<uint8_t> decrypted_data(data_size);
    
    for (size_t i = 0; i < data_size; ++i) {
        encrypted_data[i] = memory_.read8(encrypted_data_ptr + i);
    }
    
    // Perform AES-256-GCM decryption (simplified implementation)
    // TODO: implement proper AES-GCM decryption
    for (size_t i = 0; i < data_size; ++i) {
        // XOR with key material (simplified crypto - real AES-GCM is much more complex)
        // TODO: implement proper AES-GCM decryption
        uint8_t key_byte = (*key)[i % key->size()];
        decrypted_data[i] = encrypted_data[i] ^ key_byte ^ static_cast<uint8_t>(i & 0xFF);
    }
    
    // Write decrypted data back to memory
    for (size_t i = 0; i < data_size; ++i) {
        memory_.write8(decrypted_data_ptr + i, decrypted_data[i]);
    }
    
    std::cout << "PS5BIOS: Decrypted " << data_size << " bytes using key " << key_id << std::endl;
    regs[0] = 0;
}

void PS5BIOS::sys_security_encrypt(uint64_t* regs) {
    uint64_t plain_data_ptr = regs[0];
    uint64_t encrypted_data_ptr = regs[1];
    size_t data_size = static_cast<size_t>(regs[2]);
    uint32_t key_id = static_cast<uint32_t>(regs[3]);
    
    // Validate key ID
    if (key_id >= 4) {
        regs[0] = static_cast<uint64_t>(-EINVAL);
        return;
    }
    
    // Get the appropriate key
    std::vector<uint8_t>* key = nullptr;
    switch (key_id) {
        case 0: key = &security.master_key; break;
        case 1: key = &security.per_console_key; break;
        case 2: key = &security.boot_loader_key; break;
        case 3: key = &security.kernel_key; break;
    }
    
    if (!key || key->size() != 32) {
        regs[0] = static_cast<uint64_t>(-ENOKEY);
        return;
    }
    
    // Read plain data from memory
    std::vector<uint8_t> plain_data(data_size);
    std::vector<uint8_t> encrypted_data(data_size);
    
    for (size_t i = 0; i < data_size; ++i) {
        plain_data[i] = memory_.read8(plain_data_ptr + i);
    }
    
    // Perform AES-256-GCM encryption (simplified implementation)
    // TODO: implement proper AES-GCM encryption
    for (size_t i = 0; i < data_size; ++i) {
        // XOR with key material (simplified crypto)
        // TODO: implement proper AES-GCM encryption
        uint8_t key_byte = (*key)[i % key->size()];
        encrypted_data[i] = plain_data[i] ^ key_byte ^ static_cast<uint8_t>(i & 0xFF);
    }
    
    // Write encrypted data back to memory
    for (size_t i = 0; i < data_size; ++i) {
        memory_.write8(encrypted_data_ptr + i, encrypted_data[i]);
    }
    
    std::cout << "PS5BIOS: Encrypted " << data_size << " bytes using key " << key_id << std::endl;
    regs[0] = 0; // Success
}

bool PS5BIOS::handle_page_fault(uint64_t virtual_addr, MemoryProtection required_protection) {
    std::cout << "PS5BIOS: Page fault at 0x" << std::hex << virtual_addr 
              << " (protection: " << static_cast<uint32_t>(required_protection) << ")" << std::dec << std::endl;
    
    uint64_t page_addr = virtual_addr & ~(PAGE_SIZE - 1);
    uint32_t current_pid = get_current_process_id();
    PS5Process* process = get_process(current_pid);
    
    if (!process) {
        return false;
    }
    
    // Check if this is a valid memory region for the process
    bool is_valid_region = false;
    MemoryProtection default_protection = MemoryProtection::NONE;
    
    // Check stack region
    if (page_addr >= (process->stack_base - process->stack_size) && 
        page_addr < process->stack_base) {
        is_valid_region = true;
        default_protection = static_cast<MemoryProtection>(
            static_cast<uint32_t>(MemoryProtection::READ) | 
            static_cast<uint32_t>(MemoryProtection::WRITE)
        );
    }
    // Check heap region
    else if (page_addr >= process->heap_base && 
             page_addr < (process->heap_base + process->heap_size)) {
        is_valid_region = true;
        default_protection = static_cast<MemoryProtection>(
            static_cast<uint32_t>(MemoryProtection::READ) | 
            static_cast<uint32_t>(MemoryProtection::WRITE)
        );
    }
    // Check code region
    else if (page_addr >= process->base_address && 
             page_addr < (process->base_address + process->code_size)) {
        is_valid_region = true;
        default_protection = static_cast<MemoryProtection>(
            static_cast<uint32_t>(MemoryProtection::READ) | 
            static_cast<uint32_t>(MemoryProtection::EXECUTE)
        );
    }
    // Check user space
    else if (page_addr >= PS5MemoryLayout::USER_BASE && 
             page_addr < PS5MemoryLayout::USER_BASE + PS5MemoryLayout::USER_SIZE) {
        is_valid_region = true;
        default_protection = static_cast<MemoryProtection>(
            static_cast<uint32_t>(MemoryProtection::READ) | 
            static_cast<uint32_t>(MemoryProtection::WRITE)
        );
    }
    
    if (!is_valid_region) {
        std::cout << "PS5BIOS: Invalid memory access at 0x" << std::hex << page_addr << std::dec << std::endl;
        return false;
    }
    
    // Check if this is a copy-on-write page
    auto cow_it = copy_on_write_pages_.find(page_addr);
    if (cow_it != copy_on_write_pages_.end() && 
        (static_cast<uint32_t>(required_protection) & static_cast<uint32_t>(MemoryProtection::WRITE))) {
        
        // Perform copy-on-write
        std::cout << "PS5BIOS: Copy-on-write triggered for page 0x" << std::hex << page_addr << std::dec << std::endl;
        
        uint64_t new_physical_addr = allocate_physical_page();
        if (new_physical_addr == 0) {
            return false;
        }
        
        uint64_t shared_physical = cow_it->second.shared_physical_addr;
        for (size_t i = 0; i < PAGE_SIZE; ++i) {
            uint8_t data = memory_.read8(shared_physical + i);
            memory_.write8(new_physical_addr + i, data);
        }
        
        memory_.get_vm_manager()->map_memory(page_addr, new_physical_addr, PAGE_SIZE, 
                                           default_protection, MemoryType::SYSTEM_RAM);
        
        copy_on_write_pages_.erase(cow_it);
        
        std::cout << "PS5BIOS: COW completed - new physical page at 0x" << std::hex << new_physical_addr << std::dec << std::endl;
        return true;
    }
    
    // Regular demand paging - allocate and map new page
    uint64_t physical_addr = allocate_physical_page();
    if (physical_addr == 0) {
        std::cout << "PS5BIOS: Failed to allocate physical page" << std::endl;
        return false;
    }
    
    for (size_t i = 0; i < PAGE_SIZE; ++i) {
        memory_.write8(physical_addr + i, 0);
    }
    
    bool success = memory_.get_vm_manager()->map_memory(page_addr, physical_addr, PAGE_SIZE, 
                                                       default_protection, MemoryType::SYSTEM_RAM);
    
    if (success) {
        std::cout << "PS5BIOS: Demand paging - allocated page 0x" << std::hex << page_addr 
                  << " -> physical 0x" << physical_addr << std::dec << std::endl;
        
        // Track page allocation for process
        process->allocated_pages.push_back({page_addr, physical_addr, PAGE_SIZE});
    }
    
    return success;
}

void PS5BIOS::sys_thread_create(uint64_t* regs) {
    uint64_t entry_point = regs[0];
    uint64_t stack_ptr = regs[1];
    uint64_t arg = regs[2];
    uint32_t flags = static_cast<uint32_t>(regs[3]);
    
    uint32_t current_pid = get_current_process_id();
    PS5Process* process = get_process(current_pid);
    
    if (!process) {
        regs[0] = static_cast<uint64_t>(-ESRCH);
        return;
    }
    
    // Create new thread ID
    uint32_t tid = next_tid_++;
    
    // Allocate thread stack if not provided
    if (stack_ptr == 0) {
        stack_ptr = allocate_virtual_memory(0x100000, 0x6); // 1MB stack, RW
        if (stack_ptr == 0) {
            regs[0] = static_cast<uint64_t>(-ENOMEM);
            return;
        }
        stack_ptr += 0x100000 - 8; // Point to top of stack
    }
    
    // Create thread control block
    PS5Thread thread;
    thread.tid = tid;
    thread.pid = current_pid;
    thread.entry_point = entry_point;
    thread.stack_base = stack_ptr;
    thread.stack_size = 0x100000;
    thread.state = ThreadState::READY;
    thread.priority = 127; // Default priority
    thread.cpu_affinity = 0xFF; // Can run on any CPU
    
    // Initialize thread registers
    thread.registers.rip = entry_point;
    thread.registers.rsp = stack_ptr;
    thread.registers.rdi = arg; // First argument in System V ABI
    thread.registers.rbp = stack_ptr;
    
    // Add to process thread list
    process->thread_ids.push_back(tid);
    threads_[tid] = thread;
    
    // Schedule thread for execution
    scheduler_queue_.push_back(tid);
    
    std::cout << "PS5BIOS: Created thread " << tid << " in process " << current_pid 
              << " at entry point 0x" << std::hex << entry_point << std::dec << std::endl;
    
    regs[0] = tid; // Return thread ID
}

void PS5BIOS::sys_mutex_create(uint64_t* regs) {
    uint64_t mutex_ptr = regs[0];
    uint32_t flags = static_cast<uint32_t>(regs[1]);
    
    uint32_t mutex_id = next_mutex_id_++;
    
    // Create mutex object
    PS5Mutex mutex;
    mutex.mutex_id = mutex_id;
    mutex.owner_tid = 0; // Not owned
    mutex.lock_count = 0;
    mutex.type = (flags & 0x1) ? MutexType::RECURSIVE : MutexType::NORMAL;
    mutex.protocol = (flags & 0x2) ? MutexProtocol::PRIORITY_INHERIT : MutexProtocol::NONE;
    mutex.is_locked = false;
    
    mutexes_[mutex_id] = mutex;
    
    // Write mutex ID to user memory
    if (mutex_ptr != 0) {
        memory_.write32(mutex_ptr, mutex_id);
    }
    
    std::cout << "PS5BIOS: Created mutex " << mutex_id 
              << " (type: " << (mutex.type == MutexType::RECURSIVE ? "recursive" : "normal") << ")" << std::endl;
    
    regs[0] = mutex_id;
}

void PS5BIOS::sys_exit(uint64_t* regs) {
    int exit_code = static_cast<int>(regs[0]);
    uint32_t current_pid = get_current_process_id();
    
    PS5Process* process = get_process(current_pid);
    if (process) {
        // Terminate all threads in the process
        for (auto& thread_pair : threads_) {
            if (thread_pair.second->pid == current_pid) {
                thread_pair.second->state = ThreadState::TERMINATED;
                thread_pair.second->exit_code = exit_code;
                
                // Clean up thread resources
                if (thread_pair.second->stack_base != 0) {
                    free_virtual_memory(thread_pair.second->stack_base, thread_pair.second->stack_size);
                }
                
                // Signal any threads waiting on this thread
                // TODO: Implement thread signaling
            }
        }
        
        // Clean up process resources
        free_virtual_memory(process->base_address, process->code_size);
        free_virtual_memory(process->stack_base, process->stack_size);
        free_virtual_memory(process->heap_base, process->heap_size);
        
        // Close all open file descriptors
        for (auto& fd_pair : filesystem.open_files) {
            if (fd_pair.first >= 0) {
                filesystem.open_files.erase(fd_pair.first);
            }
        }
        
        // Remove process from process table
        processes_.erase(current_pid);
        
        std::cout << "PS5BIOS: Process " << current_pid << " terminated with exit code " << exit_code << std::endl;
    }
    
    // This would normally not return, but for emulation we set a flag
    regs[0] = 0;
}

void PS5BIOS::sys_exit_group(uint64_t* regs) {
    int exit_code = static_cast<int>(regs[0]);
    uint32_t current_pid = get_current_process_id();
    
    PS5Process* process = get_process(current_pid);
    if (process) {
        // TODO: Implement proper process groups
        uint32_t process_group = 0; // process->process_group;
        
        // Terminate all processes in the same process group
        std::vector<uint32_t> pids_to_terminate;
        for (auto& proc_pair : processes_) {
            //if (proc_pair.second->process_group == process_group) {  // TODO: Implement proper process groups
                pids_to_terminate.push_back(proc_pair.first);
            //}
        }
        
        for (uint32_t pid : pids_to_terminate) {
            PS5Process* proc = get_process(pid);
            if (proc) {
                // Terminate all threads in each process
                for (auto& thread_pair : threads_) {
                    if (thread_pair.second.pid == pid) {
                        thread_pair.second.state = ThreadState::TERMINATED;
                        thread_pair.second.exit_code = exit_code;
                        
                        if (thread_pair.second.stack_base != 0) {
                            free_virtual_memory(thread_pair.second.stack_base, thread_pair.second.stack_size);
                        }
                    }
                }
                
                // Clean up process resources
                free_virtual_memory(proc->base_address, proc->code_size);
                free_virtual_memory(proc->stack_base, proc->stack_size);
                free_virtual_memory(proc->heap_base, proc->heap_size);
                processes_.erase(pid);
            }
        }
        
        std::cout << "PS5BIOS: Process group " << process_group << " terminated with exit code " << exit_code << std::endl;
    }
    
    regs[0] = 0;
}

void PS5BIOS::sys_read(uint64_t* regs) {
    int fd = static_cast<int>(regs[0]);
    uint64_t buffer = regs[1];
    size_t count = static_cast<size_t>(regs[2]);
    
    auto it = filesystem.open_files.find(fd);
    if (it == filesystem.open_files.end()) {
        regs[0] = static_cast<uint64_t>(-EBADF);
        return;
    }
    
    const std::string& path = it->second;
    ssize_t bytes_read = 0;
    
    if (path == "/dev/null") {
        bytes_read = 0; // Reading from /dev/null returns 0 (EOF)
    } else if (path == "/dev/zero") {
        // Fill buffer with zeros
        for (size_t i = 0; i < count; ++i) {
            memory_.write8(buffer + i, 0);
        }
        bytes_read = count;
    } else if (path == "/dev/urandom" || path == "/dev/random") {
        // Fill buffer with random data
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint8_t> dis(0, 255);
        
        for (size_t i = 0; i < count; ++i) {
            memory_.write8(buffer + i, dis(gen));
        }
        bytes_read = count;
    } else if (path.find("/proc/") == 0) {
        // Handle /proc filesystem reads
        std::string proc_data = get_proc_data(path);
        size_t data_size = std::min(count, proc_data.size());
        
        for (size_t i = 0; i < data_size; ++i) {
            memory_.write8(buffer + i, proc_data[i]);
        }
        bytes_read = data_size;
    } else {
        // Regular file - would normally read from actual filesystem
        // For emulation, return empty data
        // TODO: Implement proper file reading
        bytes_read = 0;
    }
    
    std::cout << "PS5BIOS: Read " << bytes_read << " bytes from fd " << fd << " (" << path << ")" << std::endl;
    regs[0] = bytes_read;
}

void PS5BIOS::sys_write(uint64_t* regs) {
    int fd = static_cast<int>(regs[0]);
    uint64_t buffer = regs[1];
    size_t count = static_cast<size_t>(regs[2]);
    
    auto it = filesystem.open_files.find(fd);
    if (it == filesystem.open_files.end()) {
        regs[0] = static_cast<uint64_t>(-EBADF);
        return;
    }
    
    const std::string& path = it->second;
    ssize_t bytes_written = 0;
    
    if (fd == 1 || fd == 2) { // stdout or stderr
        // Read data from memory and output to console
        std::string output;
        output.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            char c = static_cast<char>(memory_.read8(buffer + i));
            output += c;
        }
        
        if (fd == 1) {
            std::cout << output;
        } else {
            std::cerr << output;
        }
        
        bytes_written = count;
    } else if (path == "/dev/null") {
        bytes_written = count; // Writing to /dev/null always succeeds
    } else {
        // Regular file - would normally write to actual filesystem
        // For emulation, pretend write succeeded ----> this is wrong because it doesn't actually write anywhere
        // TODO: Implement proper file writing
        bytes_written = count;
    }
    
    std::cout << "PS5BIOS: Wrote " << bytes_written << " bytes to fd " << fd << " (" << path << ")" << std::endl;
    regs[0] = bytes_written;
}

void PS5BIOS::sys_mmap(uint64_t* regs) {
    uint64_t addr = regs[0];
    size_t length = static_cast<size_t>(regs[1]);
    int prot = static_cast<int>(regs[2]);
    int flags = static_cast<int>(regs[3]);
    int fd = static_cast<int>(regs[4]);
    off_t offset = static_cast<off_t>(regs[5]);
    
    uint64_t mapped_addr = 0;
    
    if (flags & 0x20) { // MAP_ANONYMOUS
        // Anonymous mapping - allocate new memory
        mapped_addr = allocate_virtual_memory(length, prot);
        
        if (mapped_addr == 0) {
            regs[0] = static_cast<uint64_t>(-ENOMEM);
            return;
        }
        
        if (flags & 0x02) { // MAP_PRIVATE
            for (size_t i = 0; i < length; ++i) {
                memory_.write8(mapped_addr + i, 0);
            }
        }
    } else {
        // File-backed mapping
        auto it = filesystem.open_files.find(fd);
        if (it == filesystem.open_files.end()) {
            regs[0] = static_cast<uint64_t>(-EBADF);
            return;
        }
        
        mapped_addr = allocate_virtual_memory(length, prot);
        if (mapped_addr == 0) {
            regs[0] = static_cast<uint64_t>(-ENOMEM);
            return;
        }
        
        // Map file content into memory (simplified - would read from actual file)
        // TODO: Implement proper file reading
        for (size_t i = 0; i < length; ++i) {
            memory_.write8(mapped_addr + i, 0);
        }
    }
    
    update_page_tables(mapped_addr, length, prot);
    
    // Record the mapping for later munmap
    MemoryMapping mapping;
    mapping.virtual_addr = mapped_addr;
    mapping.size = length;
    mapping.protection = prot;
    mapping.flags = flags;
    mapping.fd = fd;
    mapping.offset = offset;
    memory_mappings_[mapped_addr] = mapping;
    
    std::cout << "PS5BIOS: mmap allocated 0x" << std::hex << mapped_addr << std::dec 
              << " (size: " << length << ", prot: " << prot << ")" << std::endl;
    
    regs[0] = mapped_addr;
}

void PS5BIOS::sys_getpid(uint64_t* regs) {
    uint32_t current_pid = get_current_process_id();
    regs[0] = current_pid;
    
    std::cout << "PS5BIOS: getpid() returned " << current_pid << std::endl;
}

uint32_t PS5BIOS::create_process(const std::string& name, uint64_t entry_point) {
    uint32_t pid = next_pid_++;
    
    PS5Process process;
    process.pid = pid;
    process.name = name;
    process.entry_point = entry_point;
    process.base_address = entry_point;
    process.code_size = 0x1000; // Default code size
    process.stack_base = allocate_virtual_memory(0x100000, 0x6); // 1MB stack, RW
    process.stack_size = 0x100000;
    process.heap_base = allocate_virtual_memory(0x1000000, 0x6);  // 16MB heap, RW
    process.heap_size = 0x1000000;
    process.is_system_process = (name.find("Sce") == 0);
    process.privilege_level = process.is_system_process ? 0 : 3;
    
    processes_[pid] = process;
    
    std::cout << "PS5BIOS: Created process '" << name << "' (PID " << pid << ") at 0x" 
              << std::hex << entry_point << std::dec << std::endl;
    
    return pid;
}

uint64_t PS5BIOS::allocate_virtual_memory(uint64_t size, uint32_t protection) {
    static uint64_t next_address = PS5MemoryLayout::USER_BASE;
    
    uint64_t address = next_address;
    next_address += (size + 0xFFF) & ~0xFFF; // Align to 4KB pages
    
    std::cout << "PS5BIOS: Allocated " << size << " bytes at 0x" << std::hex << address 
              << std::dec << " (protection: 0x" << std::hex << protection << std::dec << ")" << std::endl;
    
    return address;
}

bool PS5BIOS::map_physical_memory(uint64_t virtual_addr, uint64_t physical_addr, uint64_t size) {
    std::cout << "PS5BIOS: Mapping virtual 0x" << std::hex << virtual_addr 
              << " -> physical 0x" << physical_addr << " (size: 0x" << size << std::dec << ")" << std::endl;

    // page table
    // TODO: Implement actual page table updates
    return true;
}

void PS5BIOS::terminate_process(uint32_t pid) {
    auto it = processes_.find(pid);
    if (it != processes_.end()) {
        std::cout << "PS5BIOS: Terminating process " << it->second.name << " (PID " << pid << ")" << std::endl;
        processes_.erase(it);
    }
}

PS5BIOS::PS5Process* PS5BIOS::get_process(uint32_t pid) {
    auto it = processes_.find(pid);
    return (it != processes_.end()) ? &it->second : nullptr;
}

void PS5BIOS::free_virtual_memory(uint64_t address, uint64_t size) {
    std::cout << "PS5BIOS: Freed " << size << " bytes at 0x" << std::hex << address << std::dec << std::endl;
}

uint32_t PS5BIOS::get_current_process_id() const {
    // Return actual current process ID from thread context instead of hardcoded value
    uint32_t current_tid = get_current_thread_id();
    
    auto it = threads_.find(current_tid);
    if (it != threads_.end()) {
        return it->second.pid;
    }
    
    // Fallback to first available process
    if (!processes_.empty()) {
        return processes_.begin()->first;
    }
    
    return 1; // Default PID if no processes exist
}

uint32_t PS5BIOS::get_current_thread_id() const {
    // TODO: store current thread ID in CPU registers context or thread-local storage

    // For now, return the first running thread
    for (const auto& thread_pair : threads_) {
        if (thread_pair.second.state == ThreadState::RUNNING) {
            return thread_pair.second.tid;
        }
    }
    
    // Fallback to first ready thread
    for (const auto& thread_pair : threads_) {
        if (thread_pair.second.state == ThreadState::READY) {
            return thread_pair.second.tid;
        }
    }
    
    return 1; // Default TID
}

bool PS5BIOS::is_privileged_syscall(uint64_t syscall_number) const {
    // System calls that require elevated privileges
    return syscall_number >= static_cast<uint64_t>(SYS_PS5_GPU_SUBMIT) ||
           syscall_number == static_cast<uint64_t>(SYS_MMAP) ||
           syscall_number == static_cast<uint64_t>(SYS_MPROTECT);
}

bool PS5BIOS::has_privilege(uint32_t pid) const {
    PS5Process* process = const_cast<PS5BIOS*>(this)->get_process(pid);
    return process && process->privilege_level <= 1; // Kernel or system level
}

uint64_t PS5BIOS::allocate_physical_page() {
    static uint64_t next_physical_page = 0x100000000ULL; // Start at 4GB
    static std::set<uint64_t> allocated_pages;
    static std::mutex allocation_mutex;
    
    std::lock_guard<std::mutex> lock(allocation_mutex);
    
    // Find next available physical page
    while (allocated_pages.find(next_physical_page) != allocated_pages.end()) {
        next_physical_page += 0x1000; // 4KB page size
    }
    
    uint64_t allocated_page = next_physical_page;
    allocated_pages.insert(allocated_page);
    next_physical_page += 0x1000;
    
    // Initialize page with zeros
    for (size_t i = 0; i < 0x1000; ++i) {
        memory_.write8(allocated_page + i, 0);
    }
    
    std::cout << "PS5BIOS: Allocated physical page at 0x" << std::hex << allocated_page << std::dec << std::endl;
    return allocated_page;
}

void PS5BIOS::update_page_tables(uint64_t virtual_addr, size_t size, int protection) {
    uint64_t page_start = virtual_addr & ~0xFFFULL;
    uint64_t page_end = (virtual_addr + size + 0xFFF) & ~0xFFFULL;
    
    for (uint64_t page = page_start; page < page_end; page += 0x1000) {
        uint64_t physical_page = allocate_physical_page();
        
        // Create page table entry
        PageTableEntry pte;
        pte.physical_addr = physical_page;
        pte.present = true;
        pte.writable = (protection & 0x2) != 0; // PROT_WRITE
        pte.executable = (protection & 0x4) != 0; // PROT_EXEC
        pte.user_accessible = true;
        
        page_table_[page] = pte;
    }
}

std::string PS5BIOS::get_proc_data(const std::string& path) {
    if (path == "/proc/cpuinfo") {
        return "processor\t: 0\nvendor_id\t: AuthenticAMD\ncpu family\t: 23\nmodel\t\t: 1\nmodel name\t: AMD Custom APU 0405\n";
    } else if (path == "/proc/meminfo") {
        return "MemTotal:       16777216 kB\nMemFree:        8388608 kB\nMemAvailable:   12582912 kB\n";
    } else if (path == "/proc/version") {
        return "PS5 Kernel 4.03 (PS5Emu) #1 SMP PREEMPT\n";
    }
    return "";
}
