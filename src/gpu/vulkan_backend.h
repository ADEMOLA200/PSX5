#pragma once
#ifdef PSX5_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <unordered_map>
#include <optional>

class VulkanBackend {
public:
    VulkanBackend();
    ~VulkanBackend();
    
    bool init();
    void shutdown();
    
    // Resource management
    uint32_t create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage);
    uint32_t create_image(uint32_t width, uint32_t height, VkFormat format, 
                         VkImageUsageFlags usage, VmaMemoryUsage memory_usage);
    void destroy_buffer(uint32_t buffer_id);
    void destroy_image(uint32_t image_id);
    
    // Memory mapping
    void* map_buffer(uint32_t buffer_id);
    void unmap_buffer(uint32_t buffer_id);
    
    // Command buffer utilities
    VkCommandBuffer begin_single_time_commands();
    void end_single_time_commands(VkCommandBuffer command_buffer);
    
    // Buffer operations
    void copy_buffer(uint32_t src_buffer_id, uint32_t dst_buffer_id, VkDeviceSize size);
    void transition_image_layout(uint32_t image_id, VkImageLayout old_layout, VkImageLayout new_layout);
    
    // Getters
    VkDevice get_device() const { return device_; }
    VkPhysicalDevice get_physical_device() const { return physical_device_; }
    VkInstance get_instance() const { return instance_; }
    VkQueue get_graphics_queue() const { return graphics_queue_; }
    VkQueue get_compute_queue() const { return compute_queue_; }
    VkCommandPool get_command_pool() const { return command_pool_; }
    VkDescriptorPool get_descriptor_pool() const { return descriptor_pool_; }
    VmaAllocator get_memory_allocator() const { return memory_allocator_; }
    
    bool is_initialized() const { return initialized_; }
    
private:
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphics_family;
        std::optional<uint32_t> compute_family;
        std::optional<uint32_t> transfer_family;
        
        bool is_complete() {
            return graphics_family.has_value() && 
                   compute_family.has_value() && 
                   transfer_family.has_value();
        }
    };
    
    struct VulkanBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        VkBufferUsageFlags usage = 0;
    };
    
    struct VulkanImage {
        VkImage image = VK_NULL_HANDLE;
        VkImageView image_view = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags usage = 0;
    };
    
    // Vulkan objects
    VkInstance instance_;
    VkDevice device_;
    VkPhysicalDevice physical_device_;
    VkQueue graphics_queue_;
    VkQueue compute_queue_;
    VkQueue transfer_queue_;
    VkCommandPool command_pool_;
    VkDescriptorPool descriptor_pool_;
    VmaAllocator memory_allocator_;
    
    // Device properties
    VkPhysicalDeviceProperties device_properties_;
    VkPhysicalDeviceFeatures device_features_;
    VkPhysicalDeviceMemoryProperties memory_properties_;
    QueueFamilyIndices queue_family_indices_;
    
    // Resource tracking
    std::unordered_map<uint32_t, VulkanBuffer> buffers_;
    std::unordered_map<uint32_t, VulkanImage> images_;
    uint32_t next_resource_id_ = 1;
    
    bool initialized_;
    
    // Initialization helpers
    bool create_instance();
    bool pick_physical_device();
    bool create_logical_device();
    bool create_command_pool();
    bool create_descriptor_pool();
    bool create_memory_allocator();
    
    // Device selection
    int rate_device_suitability(VkPhysicalDevice device);
    QueueFamilyIndices find_queue_families(VkPhysicalDevice device);
};
#endif
