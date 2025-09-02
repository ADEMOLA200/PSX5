#pragma once
#include "types.h"
#include <unordered_map>
#include <mutex>
#include <atomic>

// PS5 Memory Layout Constants
constexpr uint64_t PS5_USER_MEMORY_BASE = 0x100000000ULL;  // 4GB
constexpr uint64_t PS5_USER_MEMORY_SIZE = 0x200000000ULL;  // 8GB
constexpr uint64_t PS5_KERNEL_MEMORY_BASE = 0x800000000ULL; // 32GB
constexpr uint64_t PS5_KERNEL_MEMORY_SIZE = 0x100000000ULL; // 4GB
constexpr uint64_t PS5_GPU_MEMORY_BASE = 0x900000000ULL;   // 36GB
constexpr uint64_t PS5_GPU_MEMORY_SIZE = 0x400000000ULL;   // 16GB

constexpr size_t PAGE_SIZE = 4096;
constexpr size_t LARGE_PAGE_SIZE = 2 * 1024 * 1024; // 2MB
constexpr size_t HUGE_PAGE_SIZE = 1024 * 1024 * 1024; // 1GB

enum class MemoryProtection : uint32_t {
    NONE = 0,
    READ = 1,
    WRITE = 2,
    EXECUTE = 4,
    READ_WRITE = READ | WRITE,
    READ_EXECUTE = READ | EXECUTE,
    READ_WRITE_EXECUTE = READ | WRITE | EXECUTE
};

enum class MemoryType : uint32_t {
    SYSTEM_RAM,
    GPU_MEMORY,
    SHARED_MEMORY,
    DEVICE_MEMORY,
    KERNEL_MEMORY
};

struct PageTableEntry {
    uint64_t physical_addr : 40;
    uint64_t present : 1;
    uint64_t writable : 1;
    uint64_t user_accessible : 1;
    uint64_t write_through : 1;
    uint64_t cache_disabled : 1;
    uint64_t accessed : 1;
    uint64_t dirty : 1;
    uint64_t page_size : 1;
    uint64_t global : 1;
    uint64_t available : 3;
    uint64_t no_execute : 1;
    uint64_t reserved : 11;
};

struct MemoryRegion {
    uint64_t virtual_addr;
    uint64_t physical_addr;
    size_t size;
    MemoryProtection protection;
    MemoryType type;
    bool is_mapped;
    std::string name;
};

struct TLBEntry {
    uint64_t virtual_page;
    uint64_t physical_page;
    MemoryProtection protection;
    bool valid;
    uint32_t asid; // Address Space ID
    uint64_t last_access;
};

class VirtualMemoryManager {
private:
    std::unordered_map<uint64_t, PageTableEntry> page_table;
    std::unordered_map<uint64_t, MemoryRegion> memory_regions;
    std::vector<TLBEntry> tlb_cache;
    std::mutex memory_mutex;
    std::atomic<uint64_t> next_physical_page;
    
    // Memory allocation tracking
    std::unordered_map<uint64_t, size_t> allocated_blocks;
    std::vector<std::pair<uint64_t, size_t>> free_blocks;
    
    std::function<bool(uint64_t, MemoryProtection)> page_fault_handler;
    
    bool allocate_physical_pages(uint64_t virtual_addr, size_t size, MemoryProtection protection);
    void update_tlb(uint64_t virtual_page, uint64_t physical_page, MemoryProtection protection);
    TLBEntry* find_tlb_entry(uint64_t virtual_page);
    void invalidate_tlb_entry(uint64_t virtual_page);

public:
    VirtualMemoryManager();
    ~VirtualMemoryManager();
    
    bool map_memory(uint64_t virtual_addr, uint64_t physical_addr, size_t size, 
                   MemoryProtection protection, MemoryType type, const std::string& name = "");
    bool unmap_memory(uint64_t virtual_addr, size_t size);
    bool protect_memory(uint64_t virtual_addr, size_t size, MemoryProtection protection);
    
    uint64_t allocate_virtual_memory(size_t size, MemoryProtection protection, MemoryType type);
    bool free_virtual_memory(uint64_t virtual_addr);
    
    bool translate_address(uint64_t virtual_addr, uint64_t& physical_addr, MemoryProtection required_protection = MemoryProtection::READ);
    MemoryRegion* find_region(uint64_t virtual_addr);
    
    void set_page_fault_handler(std::function<bool(uint64_t, MemoryProtection)> handler);
    void flush_tlb();
    void invalidate_page(uint64_t virtual_addr);
    
    std::vector<MemoryRegion> get_memory_map() const;
    size_t get_total_allocated() const;
    size_t get_total_free() const;
};

class Memory {
public:
    using WriteObserver = std::function<void(size_t,size_t)>;
    void set_write_observer(WriteObserver cb) { write_observer = std::move(cb); }

private:
    WriteObserver write_observer;
    std::vector<uint8_t> bytes;
    std::unique_ptr<VirtualMemoryManager> vm_manager;
    std::mutex access_mutex;
    
    // Memory access statistics
    std::atomic<uint64_t> read_count{0};
    std::atomic<uint64_t> write_count{0};
    std::atomic<uint64_t> cache_hits{0};
    std::atomic<uint64_t> cache_misses{0};
    
    // Cache simulation
    // TODO: implement cache access and replacement policies
    struct CacheLine {
        uint64_t tag;
        bool valid;
        bool dirty;
        uint64_t last_access;
        uint8_t data[64]; // 64-byte cache line
    };
    
    static constexpr size_t CACHE_SIZE = 32 * 1024; // 32KB L1 cache
    static constexpr size_t CACHE_LINE_SIZE = 64;
    static constexpr size_t CACHE_WAYS = 8;
    static constexpr size_t CACHE_SETS = CACHE_SIZE / (CACHE_LINE_SIZE * CACHE_WAYS);
    
    std::array<std::array<CacheLine, CACHE_WAYS>, CACHE_SETS> l1_cache;
    std::atomic<uint64_t> cache_access_counter{0};
    
    bool access_cache(uint64_t addr, uint8_t* data, size_t len, bool is_write);
    void invalidate_cache_line(uint64_t addr);

public:
    explicit Memory(size_t size);
    ~Memory();
    
    bool load(size_t addr, uint8_t* dst, size_t len) const;
    bool store(size_t addr, const uint8_t* src, size_t len);
    
    uint8_t  read8 (size_t addr) const;
    uint16_t read16(size_t addr) const;
    uint32_t read32(size_t addr) const;
    uint64_t read64(size_t addr) const;
    
    void     write8 (size_t addr, uint8_t  v);
    void     write16(size_t addr, uint16_t v);
    void     write32(size_t addr, uint32_t v);
    void     write64(size_t addr, uint64_t v);
    
    // Virtual memory interface
    bool map_region(uint64_t virtual_addr, size_t size, MemoryProtection protection, MemoryType type);
    bool unmap_region(uint64_t virtual_addr, size_t size);
    uint64_t allocate_memory(size_t size, MemoryProtection protection, MemoryType type);
    bool free_memory(uint64_t virtual_addr);
    
    // PS5 specific memory operations
    bool allocate_gpu_memory(size_t size, uint64_t& gpu_addr);
    bool create_shared_memory(size_t size, uint64_t& shared_addr);
    bool map_device_memory(uint64_t device_addr, size_t size, uint64_t& virtual_addr);
    
    // Memory protection and security
    bool set_memory_protection(uint64_t addr, size_t size, MemoryProtection protection);
    bool is_address_valid(uint64_t addr, size_t size, MemoryProtection required_protection) const;
    
    // Cache management
    void flush_cache();
    void invalidate_cache();
    void prefetch(uint64_t addr, size_t size);
    
    // Statistics and debugging
    struct MemoryStats {
        uint64_t total_reads;
        uint64_t total_writes;
        uint64_t cache_hit_rate;
        size_t total_allocated;
        size_t total_free;
        size_t fragmentation_ratio;
    };
    
    MemoryStats get_statistics() const;
    std::vector<MemoryRegion> get_memory_map() const;
    void dump_memory_state() const;
    
    size_t size() const { return bytes.size(); }
    uint8_t* data() { return bytes.data(); }
    VirtualMemoryManager* get_vm_manager() { return vm_manager.get(); }
};
