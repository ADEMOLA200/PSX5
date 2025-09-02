#pragma once

#ifdef PSX5_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#endif

class VulkanGlfw {
public:
    VulkanGlfw() = default;
    ~VulkanGlfw();
    bool init(int width = 800, int height = 600, const char* title = "psx5 window");
    void shutdown();
    void present_clear_frame();
    bool valid() const { return initialized_; }
private:
    bool initialized_{false};
#ifdef PSX5_ENABLE_VULKAN
    VkInstance instance_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkQueue graphics_queue_{VK_NULL_HANDLE};
    VkQueue present_queue_{VK_NULL_HANDLE};
    VkCommandPool command_pool_{VK_NULL_HANDLE};
    VkRenderPass render_pass_{VK_NULL_HANDLE};
    VkPipeline graphics_pipeline_{VK_NULL_HANDLE};
    VkPipelineLayout pipeline_layout_{VK_NULL_HANDLE};
    uint32_t current_frame_{0};
    // Note: For brevity a bunch of Vk objects are omitted; this is a scaffold.
#endif
};
