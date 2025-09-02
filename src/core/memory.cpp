#include "memory.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <iomanip>
#include <iostream>


VirtualMemoryManager::VirtualMemoryManager() : next_physical_page(0x1000) {
    tlb_cache.resize(256); // 256-entry TLB
    for (auto& entry : tlb_cache) {
        entry.valid = false;
    }
    
    // Initialize free block list with entire address space
    free_blocks.push_back({PS5_USER_MEMORY_BASE, PS5_USER_MEMORY_SIZE});
}

VirtualMemoryManager::~VirtualMemoryManager() = default;

bool VirtualMemoryManager::map_memory(uint64_t virtual_addr, uint64_t physical_addr, size_t size,
                                     MemoryProtection protection, MemoryType type, const std::string& name) {
    std::lock_guard<std::mutex> lock(memory_mutex);
    
    // Align addresses to page boundaries
    uint64_t aligned_vaddr = virtual_addr & ~(PAGE_SIZE - 1);
    uint64_t aligned_paddr = physical_addr & ~(PAGE_SIZE - 1);
    size_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    // Check for overlapping regions
    for (const auto& [addr, region] : memory_regions) {
        if (aligned_vaddr < region.virtual_addr + region.size && 
            aligned_vaddr + aligned_size > region.virtual_addr) {
            return false;
        }
    }
    
    // Create page table entries
    for (uint64_t offset = 0; offset < aligned_size; offset += PAGE_SIZE) {
        uint64_t vpage = (aligned_vaddr + offset) / PAGE_SIZE;
        uint64_t ppage = (aligned_paddr + offset) / PAGE_SIZE;
        
        PageTableEntry pte = {};
        pte.physical_addr = ppage;
        pte.present = 1;
        pte.writable = (static_cast<uint32_t>(protection) & static_cast<uint32_t>(MemoryProtection::WRITE)) ? 1 : 0;
        pte.user_accessible = (type != MemoryType::KERNEL_MEMORY) ? 1 : 0;
        pte.no_execute = (static_cast<uint32_t>(protection) & static_cast<uint32_t>(MemoryProtection::EXECUTE)) ? 0 : 1;
        
        page_table[vpage] = pte;
        update_tlb(vpage, ppage, protection);
    }
    
    // Create memory region
    MemoryRegion region = {};
    region.virtual_addr = aligned_vaddr;
    region.physical_addr = aligned_paddr;
    region.size = aligned_size;
    region.protection = protection;
    region.type = type;
    region.is_mapped = true;
    region.name = name;
    
    memory_regions[aligned_vaddr] = region;
    return true;
}

bool VirtualMemoryManager::unmap_memory(uint64_t virtual_addr, size_t size) {
    std::lock_guard<std::mutex> lock(memory_mutex);
    
    uint64_t aligned_vaddr = virtual_addr & ~(PAGE_SIZE - 1);
    size_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    // Remove page table entries
    for (uint64_t offset = 0; offset < aligned_size; offset += PAGE_SIZE) {
        uint64_t vpage = (aligned_vaddr + offset) / PAGE_SIZE;
        page_table.erase(vpage);
        invalidate_tlb_entry(vpage);
    }
    
    // Remove memory region
    memory_regions.erase(aligned_vaddr);
    
    // Add to free blocks
    free_blocks.push_back({aligned_vaddr, aligned_size});
    
    return true;
}

bool VirtualMemoryManager::translate_address(uint64_t virtual_addr, uint64_t& physical_addr, MemoryProtection required_protection) {
    uint64_t vpage = virtual_addr / PAGE_SIZE;
    uint64_t offset = virtual_addr % PAGE_SIZE;
    
    // Check TLB first
    TLBEntry* tlb_entry = find_tlb_entry(vpage);
    if (tlb_entry && tlb_entry->valid) {
        // Check permissions
        if ((static_cast<uint32_t>(tlb_entry->protection) & static_cast<uint32_t>(required_protection)) == static_cast<uint32_t>(required_protection)) {
            physical_addr = tlb_entry->physical_page * PAGE_SIZE + offset;
            tlb_entry->last_access = cache_access_counter++;
            return true;
        }
    }
    
    // Check page table
    auto it = page_table.find(vpage);
    if (it != page_table.end() && it->second.present) {
        const PageTableEntry& pte = it->second;
        
        // Check permissions
        bool can_read = true;
        bool can_write = pte.writable;
        bool can_execute = !pte.no_execute;
        
        MemoryProtection page_protection = MemoryProtection::NONE;
        if (can_read) page_protection = static_cast<MemoryProtection>(static_cast<uint32_t>(page_protection) | static_cast<uint32_t>(MemoryProtection::READ));
        if (can_write) page_protection = static_cast<MemoryProtection>(static_cast<uint32_t>(page_protection) | static_cast<uint32_t>(MemoryProtection::WRITE));
        if (can_execute) page_protection = static_cast<MemoryProtection>(static_cast<uint32_t>(page_protection) | static_cast<uint32_t>(MemoryProtection::EXECUTE));
        
        if ((static_cast<uint32_t>(page_protection) & static_cast<uint32_t>(required_protection)) == static_cast<uint32_t>(required_protection)) {
            physical_addr = pte.physical_addr * PAGE_SIZE + offset;
            update_tlb(vpage, pte.physical_addr, page_protection);
            return true;
        }
    }
    
    // Page fault - call handler if available
    if (page_fault_handler) {
        return page_fault_handler(virtual_addr, required_protection);
    }
    
    return false;
}

uint64_t VirtualMemoryManager::allocate_virtual_memory(size_t size, MemoryProtection protection, MemoryType type) {
    std::lock_guard<std::mutex> lock(memory_mutex);
    
    size_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    // Find suitable free block
    for (auto it = free_blocks.begin(); it != free_blocks.end(); ++it) {
        if (it->second >= aligned_size) {
            uint64_t virtual_addr = it->first;
            
            // Allocate physical pages
            uint64_t physical_addr = next_physical_page.fetch_add(aligned_size);
            
            // Update free block
            if (it->second == aligned_size) {
                free_blocks.erase(it);
            } else {
                it->first += aligned_size;
                it->second -= aligned_size;
            }
            
            // Map the memory
            if (map_memory(virtual_addr, physical_addr, aligned_size, protection, type)) {
                allocated_blocks[virtual_addr] = aligned_size;
                return virtual_addr;
            }
            break;
        }
    }
    
    return 0; // Allocation failed
}

void VirtualMemoryManager::update_tlb(uint64_t virtual_page, uint64_t physical_page, MemoryProtection protection) {
    // Simple round-robin replacement
    static size_t tlb_index = 0;
    
    TLBEntry& entry = tlb_cache[tlb_index];
    entry.virtual_page = virtual_page;
    entry.physical_page = physical_page;
    entry.protection = protection;
    entry.valid = true;
    entry.asid = 0; // Simplified - single address space
    entry.last_access = cache_access_counter++;
    
    tlb_index = (tlb_index + 1) % tlb_cache.size();
}

TLBEntry* VirtualMemoryManager::find_tlb_entry(uint64_t virtual_page) {
    for (auto& entry : tlb_cache) {
        if (entry.valid && entry.virtual_page == virtual_page) {
            return &entry;
        }
    }
    return nullptr;
}

Memory::Memory(size_t size) : bytes(size, 0), vm_manager(std::make_unique<VirtualMemoryManager>()) {
    // Initialize cache
    for (auto& set : l1_cache) {
        for (auto& way : set) {
            way.valid = false;
            way.dirty = false;
            way.tag = 0;
            way.last_access = 0;
        }
    }
    
    // Set up initial memory mappings for PS5 layout
    vm_manager->map_memory(PS5_USER_MEMORY_BASE, 0, std::min(size, static_cast<size_t>(PS5_USER_MEMORY_SIZE)), 
                          MemoryProtection::READ_WRITE, MemoryType::SYSTEM_RAM, "User Memory");
}

Memory::~Memory() = default;

bool Memory::load(size_t addr, uint8_t* dst, size_t len) const {
    if (addr + len > bytes.size()) return false;
    
    // Try cache first
    if (len <= 64 && const_cast<Memory*>(this)->access_cache(addr, dst, len, false)) {
        const_cast<Memory*>(this)->cache_hits++;
        const_cast<Memory*>(this)->read_count++;
        return true;
    }
    
    const_cast<Memory*>(this)->cache_misses++;
    std::memcpy(dst, bytes.data() + addr, len);
    const_cast<Memory*>(this)->read_count++;
    return true;
}

bool Memory::store(size_t addr, const uint8_t* src, size_t len) {
    if (write_observer) write_observer(addr, len);
    if (addr + len > bytes.size()) return false;
    
    // Try cache first
    if (len <= 64 && access_cache(addr, const_cast<uint8_t*>(src), len, true)) {
        cache_hits++;
        write_count++;
        return true;
    }
    
    cache_misses++;
    std::memcpy(bytes.data() + addr, src, len);
    write_count++;
    return true;
}

bool Memory::access_cache(uint64_t addr, uint8_t* data, size_t len, bool is_write) {
    uint64_t cache_line_addr = addr & ~(CACHE_LINE_SIZE - 1);
    size_t set_index = (cache_line_addr / CACHE_LINE_SIZE) % CACHE_SETS;
    uint64_t tag = cache_line_addr / (CACHE_LINE_SIZE * CACHE_SETS);
    
    auto& cache_set = l1_cache[set_index];
    
    // Look for hit
    for (auto& way : cache_set) {
        if (way.valid && way.tag == tag) {
            // Cache hit
            size_t offset = addr - cache_line_addr;
            
            if (is_write) {
                std::memcpy(way.data + offset, data, len);
                way.dirty = true;
                // Write through to main memory
                std::memcpy(bytes.data() + addr, data, len);
            } else {
                std::memcpy(data, way.data + offset, len);
            }
            
            way.last_access = cache_access_counter++;
            return true;
        }
    }
    
    // Cache miss - find LRU way to replace
    auto lru_way = std::min_element(cache_set.begin(), cache_set.end(),
        [](const CacheLine& a, const CacheLine& b) {
            return a.last_access < b.last_access;
        });
    
    // Write back if dirty
    if (lru_way->valid && lru_way->dirty) {
        uint64_t writeback_addr = (lru_way->tag * CACHE_SETS + set_index) * CACHE_LINE_SIZE;
        if (writeback_addr < bytes.size()) {
            std::memcpy(bytes.data() + writeback_addr, lru_way->data, 
                       std::min(CACHE_LINE_SIZE, bytes.size() - writeback_addr));
        }
    }
    
    // Load new cache line
    lru_way->tag = tag;
    lru_way->valid = true;
    lru_way->dirty = false;
    lru_way->last_access = cache_access_counter++;
    
    if (cache_line_addr < bytes.size()) {
        size_t copy_size = std::min(CACHE_LINE_SIZE, bytes.size() - cache_line_addr);
        std::memcpy(lru_way->data, bytes.data() + cache_line_addr, copy_size);
    }
    
    // Perform the access
    size_t offset = addr - cache_line_addr;
    if (is_write) {
        std::memcpy(lru_way->data + offset, data, len);
        lru_way->dirty = true;
        // Write through to main memory
        std::memcpy(bytes.data() + addr, data, len);
    } else {
        std::memcpy(data, lru_way->data + offset, len);
    }
    
    return true;
}

uint8_t Memory::read8(size_t addr) const {
    uint64_t physical_addr;
    if (vm_manager->translate_address(addr, physical_addr, MemoryProtection::READ)) {
        addr = physical_addr;
    }
    return addr < bytes.size() ? bytes[addr] : 0;
}

uint16_t Memory::read16(size_t addr) const {
    return uint16_t(read8(addr)) | (uint16_t(read8(addr+1))<<8);
}

uint32_t Memory::read32(size_t addr) const {
    return uint32_t(read16(addr)) | (uint32_t(read16(addr+2))<<16);
}

uint64_t Memory::read64(size_t addr) const {
    return uint64_t(read32(addr)) | (uint64_t(read32(addr+4))<<32);
}

void Memory::write8(size_t addr, uint8_t v) {
    uint64_t physical_addr;
    if (vm_manager->translate_address(addr, physical_addr, MemoryProtection::WRITE)) {
        addr = physical_addr;
    }
    if (addr < bytes.size()) {
        bytes[addr] = v;
        invalidate_cache_line(addr);
    }
}

void Memory::write16(size_t addr, uint16_t v) {
    write8(addr, v & 0xFF); 
    write8(addr+1, v>>8);
}

void Memory::write32(size_t addr, uint32_t v) {
    write16(addr, v & 0xFFFF); 
    write16(addr+2, v>>16);
}

void Memory::write64(size_t addr, uint64_t v) {
    write32(addr, v & 0xFFFFFFFFu); 
    write32(addr+4, uint32_t(v>>32));
}

bool Memory::allocate_gpu_memory(size_t size, uint64_t& gpu_addr) {
    gpu_addr = vm_manager->allocate_virtual_memory(size, MemoryProtection::READ_WRITE, MemoryType::GPU_MEMORY);
    return gpu_addr != 0;
}

bool Memory::create_shared_memory(size_t size, uint64_t& shared_addr) {
    shared_addr = vm_manager->allocate_virtual_memory(size, MemoryProtection::READ_WRITE, MemoryType::SHARED_MEMORY);
    return shared_addr != 0;
}

bool Memory::set_memory_protection(uint64_t addr, size_t size, MemoryProtection protection) {
    return vm_manager->protect_memory(addr, size, protection);
}

void Memory::flush_cache() {
    for (auto& set : l1_cache) {
        for (auto& way : set) {
            if (way.valid && way.dirty) {
                // Write back dirty cache lines
                uint64_t addr = (way.tag * CACHE_SETS * CACHE_LINE_SIZE) + 
                               (&set - &l1_cache[0]) * CACHE_LINE_SIZE;
                if (addr < bytes.size()) {
                    std::memcpy(bytes.data() + addr, way.data, 
                               std::min(CACHE_LINE_SIZE, bytes.size() - addr));
                }
                way.dirty = false;
            }
        }
    }
}

void Memory::invalidate_cache_line(uint64_t addr) {
    uint64_t cache_line_addr = addr & ~(CACHE_LINE_SIZE - 1);
    size_t set_index = (cache_line_addr / CACHE_LINE_SIZE) % CACHE_SETS;
    uint64_t tag = cache_line_addr / (CACHE_LINE_SIZE * CACHE_SETS);
    
    auto& cache_set = l1_cache[set_index];
    for (auto& way : cache_set) {
        if (way.valid && way.tag == tag) {
            way.valid = false;
            break;
        }
    }
}

Memory::MemoryStats Memory::get_statistics() const {
    MemoryStats stats = {};
    stats.total_reads = read_count.load();
    stats.total_writes = write_count.load();
    
    uint64_t total_accesses = cache_hits.load() + cache_misses.load();
    stats.cache_hit_rate = total_accesses > 0 ? 
        (static_cast<double>(cache_hits.load()) / total_accesses) * 100.0 : 0.0;
    
    stats.total_allocated = vm_manager->get_total_allocated();
    stats.total_free = vm_manager->get_total_free();
    
    return stats;
}

std::vector<MemoryRegion> Memory::get_memory_map() const {
    return vm_manager->get_memory_map();
}
