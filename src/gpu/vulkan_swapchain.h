#pragma once
#ifdef PSX5_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <string>
#include <vector>

class VulkanSwapchain {
public:
    VulkanSwapchain() = default;
    ~VulkanSwapchain();
    bool init(GLFWwindow* window);
    void shutdown();
    void draw_frame();
    bool valid() const { return initialized_; }
private:
    bool initialized_{false};
    VkInstance instance_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue graphicsQueue_{VK_NULL_HANDLE};
    VkQueue presentQueue_{VK_NULL_HANDLE};
    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    std::vector<VkImage> swapImages_;
    std::vector<VkImageView> swapViews_;
    VkRenderPass renderPass_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
    // for simplicity single command buffer
    // TODO: Implement command buffer recording
    VkCommandPool cmdPool_{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> cmdBuffers_;
    size_t currentFrame_{0};
    bool create_instance(GLFWwindow* window);
    bool pick_physical_device();
    bool create_device_and_queues();
    bool create_swapchain(GLFWwindow* window);
    bool create_render_pass();
    bool create_pipeline(const std::string& vert_spv_path, const std::string& frag_spv_path);
    bool create_command_buffers();
    VkShaderModule load_spv_module(const std::string& path);
};
#endif
