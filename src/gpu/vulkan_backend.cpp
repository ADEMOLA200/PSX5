#include "vulkan_backend.h"
#ifdef PSX5_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <set>
#include <algorithm>
#include <cstring>

VulkanBackend::VulkanBackend() : instance_(VK_NULL_HANDLE), device_(VK_NULL_HANDLE), 
                                 physical_device_(VK_NULL_HANDLE), graphics_queue_(VK_NULL_HANDLE),
                                 compute_queue_(VK_NULL_HANDLE), command_pool_(VK_NULL_HANDLE),
                                 descriptor_pool_(VK_NULL_HANDLE), initialized_(false) {
    memset(&queue_family_indices_, 0, sizeof(queue_family_indices_));
}

VulkanBackend::~VulkanBackend() {
    shutdown();
}

bool VulkanBackend::init() {
    if (initialized_) return true;
    
    std::cout << "VulkanBackend: Initializing Vulkan backend..." << std::endl;
    
    if (!create_instance()) return false;
    if (!pick_physical_device()) return false;
    if (!create_logical_device()) return false;
    if (!create_command_pool()) return false;
    if (!create_descriptor_pool()) return false;
    if (!create_memory_allocator()) return false;
    
    initialized_ = true;
    std::cout << "VulkanBackend: Vulkan backend initialized successfully" << std::endl;
    return true;
}

void VulkanBackend::shutdown() {
    if (!initialized_) return;
    
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        
        // Cleanup memory allocator
        if (memory_allocator_) {
            vmaDestroyAllocator(memory_allocator_);
            memory_allocator_ = VK_NULL_HANDLE;
        }
        
        // Cleanup descriptor pool
        if (descriptor_pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
        }
        
        // Cleanup command pool
        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
        }
        
        // Cleanup buffers and images
        for (auto& buffer : buffers_) {
            if (buffer.second.buffer != VK_NULL_HANDLE) {
                vmaDestroyBuffer(memory_allocator_, buffer.second.buffer, buffer.second.allocation);
            }
        }
        buffers_.clear();
        
        for (auto& image : images_) {
            if (image.second.image != VK_NULL_HANDLE) {
                if (image.second.image_view != VK_NULL_HANDLE) {
                    vkDestroyImageView(device_, image.second.image_view, nullptr);
                }
                vmaDestroyImage(memory_allocator_, image.second.image, image.second.allocation);
            }
        }
        images_.clear();
        
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    
    initialized_ = false;
    std::cout << "VulkanBackend: Vulkan backend shutdown complete" << std::endl;
}

bool VulkanBackend::create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "PS5 Emulator";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "PS5Emu";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;
    
    std::vector<const char*> required_extensions = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME
    };
    
    std::vector<const char*> validation_layers;
#ifdef _DEBUG
    validation_layers.push_back("VK_LAYER_KHRONOS_validation");
#endif
    
    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(required_extensions.size());
    create_info.ppEnabledExtensionNames = required_extensions.data();
    create_info.enabledLayerCount = static_cast<uint32_t>(validation_layers.size());
    create_info.ppEnabledLayerNames = validation_layers.data();
    
    VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        std::cerr << "VulkanBackend: Failed to create Vulkan instance: " << result << std::endl;
        return false;
    }
    
    return true;
}

bool VulkanBackend::pick_physical_device() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
    
    if (device_count == 0) {
        std::cerr << "VulkanBackend: No Vulkan-capable devices found" << std::endl;
        return false;
    }
    
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());
    
    // Score devices and pick the best one
    int best_score = -1;
    for (const auto& device : devices) {
        int score = rate_device_suitability(device);
        if (score > best_score) {
            best_score = score;
            physical_device_ = device;
        }
    }
    
    if (physical_device_ == VK_NULL_HANDLE) {
        std::cerr << "VulkanBackend: No suitable physical device found" << std::endl;
        return false;
    }
    
    // Get device properties
    vkGetPhysicalDeviceProperties(physical_device_, &device_properties_);
    vkGetPhysicalDeviceFeatures(physical_device_, &device_features_);
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties_);
    
    std::cout << "VulkanBackend: Selected device: " << device_properties_.deviceName << std::endl;
    return true;
}

int VulkanBackend::rate_device_suitability(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceProperties(device, &properties);
    vkGetPhysicalDeviceFeatures(device, &features);
    
    int score = 0;
    
    // Prefer discrete GPUs
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }
    
    // Maximum possible size of textures affects graphics quality
    score += properties.limits.maxImageDimension2D;
    
    // Check for required features
    if (!features.geometryShader || !features.tessellationShader) {
        return 0; // Must have geometry and tessellation shaders
    }
    
    // Check queue families
    QueueFamilyIndices indices = find_queue_families(device);
    if (!indices.is_complete()) {
        return 0;
    }
    
    return score;
}

VulkanBackend::QueueFamilyIndices VulkanBackend::find_queue_families(VkPhysicalDevice device) {
    QueueFamilyIndices indices;
    
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
    
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());
    
    int i = 0;
    for (const auto& queue_family : queue_families) {
        if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics_family = i;
        }
        
        if (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT) {
            indices.compute_family = i;
        }
        
        if (queue_family.queueFlags & VK_QUEUE_TRANSFER_BIT) {
            indices.transfer_family = i;
        }
        
        if (indices.is_complete()) {
            break;
        }
        
        i++;
    }
    
    return indices;
}

bool VulkanBackend::create_logical_device() {
    QueueFamilyIndices indices = find_queue_families(physical_device_);
    queue_family_indices_ = indices;
    
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    std::set<uint32_t> unique_queue_families = {
        indices.graphics_family.value(),
        indices.compute_family.value(),
        indices.transfer_family.value()
    };
    
    float queue_priority = 1.0f;
    for (uint32_t queue_family : unique_queue_families) {
        VkDeviceQueueCreateInfo queue_create_info{};
        queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_info.queueFamilyIndex = queue_family;
        queue_create_info.queueCount = 1;
        queue_create_info.pQueuePriorities = &queue_priority;
        queue_create_infos.push_back(queue_create_info);
    }
    
    VkPhysicalDeviceFeatures device_features{};
    device_features.geometryShader = VK_TRUE;
    device_features.tessellationShader = VK_TRUE;
    device_features.fillModeNonSolid = VK_TRUE;
    device_features.samplerAnisotropy = VK_TRUE;
    device_features.textureCompressionBC = VK_TRUE;
    
    std::vector<const char*> device_extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_MAINTENANCE1_EXTENSION_NAME,
        VK_KHR_MAINTENANCE2_EXTENSION_NAME,
        VK_KHR_MAINTENANCE3_EXTENSION_NAME
    };
    
    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
    create_info.pQueueCreateInfos = queue_create_infos.data();
    create_info.pEnabledFeatures = &device_features;
    create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
    create_info.ppEnabledExtensionNames = device_extensions.data();
    
    if (vkCreateDevice(physical_device_, &create_info, nullptr, &device_) != VK_SUCCESS) {
        std::cerr << "VulkanBackend: Failed to create logical device" << std::endl;
        return false;
    }
    
    // Get queue handles
    vkGetDeviceQueue(device_, indices.graphics_family.value(), 0, &graphics_queue_);
    vkGetDeviceQueue(device_, indices.compute_family.value(), 0, &compute_queue_);
    vkGetDeviceQueue(device_, indices.transfer_family.value(), 0, &transfer_queue_);
    
    return true;
}

bool VulkanBackend::create_command_pool() {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family_indices_.graphics_family.value();
    
    if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) != VK_SUCCESS) {
        std::cerr << "VulkanBackend: Failed to create command pool" << std::endl;
        return false;
    }
    
    return true;
}

bool VulkanBackend::create_descriptor_pool() {
    std::array<VkDescriptorPoolSize, 4> pool_sizes{};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_sizes[0].descriptorCount = 1000;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_sizes[1].descriptorCount = 1000;
    pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sizes[2].descriptorCount = 1000;
    pool_sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool_sizes[3].descriptorCount = 1000;
    
    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    
    if (vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
        std::cerr << "VulkanBackend: Failed to create descriptor pool" << std::endl;
        return false;
    }
    
    return true;
}

bool VulkanBackend::create_memory_allocator() {
    VmaAllocatorCreateInfo allocator_info{};
    allocator_info.vulkanApiVersion = VK_API_VERSION_1_2;
    allocator_info.physicalDevice = physical_device_;
    allocator_info.device = device_;
    allocator_info.instance = instance_;
    
    if (vmaCreateAllocator(&allocator_info, &memory_allocator_) != VK_SUCCESS) {
        std::cerr << "VulkanBackend: Failed to create memory allocator" << std::endl;
        return false;
    }
    
    return true;
}

uint32_t VulkanBackend::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = memory_usage;
    
    VulkanBuffer vulkan_buffer{};
    if (vmaCreateBuffer(memory_allocator_, &buffer_info, &alloc_info,
                       &vulkan_buffer.buffer, &vulkan_buffer.allocation, nullptr) != VK_SUCCESS) {
        std::cerr << "VulkanBackend: Failed to create buffer" << std::endl;
        return 0;
    }
    
    vulkan_buffer.size = size;
    vulkan_buffer.usage = usage;
    
    uint32_t buffer_id = next_resource_id_++;
    buffers_[buffer_id] = vulkan_buffer;
    
    return buffer_id;
}

uint32_t VulkanBackend::create_image(uint32_t width, uint32_t height, VkFormat format, 
                                    VkImageUsageFlags usage, VmaMemoryUsage memory_usage) {
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = width;
    image_info.extent.height = height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo alloc_info{};
    alloc_info.usage = memory_usage;
    
    VulkanImage vulkan_image{};
    if (vmaCreateImage(memory_allocator_, &image_info, &alloc_info,
                      &vulkan_image.image, &vulkan_image.allocation, nullptr) != VK_SUCCESS) {
        std::cerr << "VulkanBackend: Failed to create image" << std::endl;
        return 0;
    }
    
    // Create image view
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = vulkan_image.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(device_, &view_info, nullptr, &vulkan_image.image_view) != VK_SUCCESS) {
        std::cerr << "VulkanBackend: Failed to create image view" << std::endl;
        vmaDestroyImage(memory_allocator_, vulkan_image.image, vulkan_image.allocation);
        return 0;
    }
    
    vulkan_image.width = width;
    vulkan_image.height = height;
    vulkan_image.format = format;
    vulkan_image.usage = usage;
    
    uint32_t image_id = next_resource_id_++;
    images_[image_id] = vulkan_image;
    
    return image_id;
}

void VulkanBackend::destroy_buffer(uint32_t buffer_id) {
    auto it = buffers_.find(buffer_id);
    if (it != buffers_.end()) {
        vmaDestroyBuffer(memory_allocator_, it->second.buffer, it->second.allocation);
        buffers_.erase(it);
    }
}

void VulkanBackend::destroy_image(uint32_t image_id) {
    auto it = images_.find(image_id);
    if (it != images_.end()) {
        if (it->second.image_view != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, it->second.image_view, nullptr);
        }
        vmaDestroyImage(memory_allocator_, it->second.image, it->second.allocation);
        images_.erase(it);
    }
}

void* VulkanBackend::map_buffer(uint32_t buffer_id) {
    auto it = buffers_.find(buffer_id);
    if (it == buffers_.end()) return nullptr;
    
    void* data;
    if (vmaMapMemory(memory_allocator_, it->second.allocation, &data) != VK_SUCCESS) {
        return nullptr;
    }
    
    return data;
}

void VulkanBackend::unmap_buffer(uint32_t buffer_id) {
    auto it = buffers_.find(buffer_id);
    if (it != buffers_.end()) {
        vmaUnmapMemory(memory_allocator_, it->second.allocation);
    }
}

VkCommandBuffer VulkanBackend::begin_single_time_commands() {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool_;
    alloc_info.commandBufferCount = 1;
    
    VkCommandBuffer command_buffer;
    vkAllocateCommandBuffers(device_, &alloc_info, &command_buffer);
    
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(command_buffer, &begin_info);
    
    return command_buffer;
}

void VulkanBackend::end_single_time_commands(VkCommandBuffer command_buffer) {
    vkEndCommandBuffer(command_buffer);
    
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    
    vkQueueSubmit(graphics_queue_, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue_);
    
    vkFreeCommandBuffers(device_, command_pool_, 1, &command_buffer);
}

void VulkanBackend::copy_buffer(uint32_t src_buffer_id, uint32_t dst_buffer_id, VkDeviceSize size) {
    auto src_it = buffers_.find(src_buffer_id);
    auto dst_it = buffers_.find(dst_buffer_id);
    
    if (src_it == buffers_.end() || dst_it == buffers_.end()) {
        std::cerr << "VulkanBackend: Invalid buffer IDs for copy operation" << std::endl;
        return;
    }
    
    VkCommandBuffer command_buffer = begin_single_time_commands();
    
    VkBufferCopy copy_region{};
    copy_region.size = size;
    vkCmdCopyBuffer(command_buffer, src_it->second.buffer, dst_it->second.buffer, 1, &copy_region);
    
    end_single_time_commands(command_buffer);
}

void VulkanBackend::transition_image_layout(uint32_t image_id, VkImageLayout old_layout, VkImageLayout new_layout) {
    auto it = images_.find(image_id);
    if (it == images_.end()) {
        std::cerr << "VulkanBackend: Invalid image ID for layout transition" << std::endl;
        return;
    }
    
    VkCommandBuffer command_buffer = begin_single_time_commands();
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = it->second.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    VkPipelineStageFlags source_stage;
    VkPipelineStageFlags destination_stage;
    
    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        
        source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        std::cerr << "VulkanBackend: Unsupported layout transition" << std::endl;
        return;
    }
    
    vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    end_single_time_commands(command_buffer);
}

#endif
