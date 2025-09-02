#include "gpu/vulkan_glfw.h"
#include <iostream>
#include <vector>
#include <set>
#include <algorithm>
#include <fstream>

#ifdef PSX5_ENABLE_GLFW
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#endif

#ifdef PSX5_ENABLE_VULKAN
#include <vulkan/vulkan.h>
#endif

VulkanGlfw::VulkanGlfw() : window_(nullptr), instance_(VK_NULL_HANDLE), device_(VK_NULL_HANDLE),
                           physical_device_(VK_NULL_HANDLE), surface_(VK_NULL_HANDLE),
                           swapchain_(VK_NULL_HANDLE), graphics_queue_(VK_NULL_HANDLE),
                           present_queue_(VK_NULL_HANDLE), command_pool_(VK_NULL_HANDLE),
                           render_pass_(VK_NULL_HANDLE), graphics_pipeline_(VK_NULL_HANDLE),
                           pipeline_layout_(VK_NULL_HANDLE), current_frame_(0) {}

VulkanGlfw::~VulkanGlfw() { 
    shutdown(); 
}

bool VulkanGlfw::init(int width, int height, const char* title) {
#ifdef PSX5_ENABLE_GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW" << std::endl;
        return false;
    }
    
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    
    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        return false;
    }
    
    width_ = width;
    height_ = height;
    
    // Initialize Vulkan
    if (!create_instance()) return false;
    if (!create_surface()) return false;
    if (!pick_physical_device()) return false;
    if (!create_logical_device()) return false;
    if (!create_swapchain()) return false;
    if (!create_image_views()) return false;
    if (!create_render_pass()) return false;
    if (!create_graphics_pipeline()) return false;
    if (!create_framebuffers()) return false;
    if (!create_command_pool()) return false;
    if (!create_command_buffers()) return false;
    if (!create_sync_objects()) return false;
    
    initialized_ = true;
    std::cout << "Vulkan initialization complete" << std::endl;
    return true;
#else
    std::cerr << "GLFW support not enabled at build time" << std::endl;
    return false;
#endif
}

void VulkanGlfw::shutdown() {
#ifdef PSX5_ENABLE_VULKAN
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        
        // Cleanup sync objects
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (render_finished_semaphores_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, render_finished_semaphores_[i], nullptr);
            }
            if (image_available_semaphores_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, image_available_semaphores_[i], nullptr);
            }
            if (in_flight_fences_[i] != VK_NULL_HANDLE) {
                vkDestroyFence(device_, in_flight_fences_[i], nullptr);
            }
        }
        
        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
        }
        
        for (auto framebuffer : swapchain_framebuffers_) {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
        
        if (graphics_pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, graphics_pipeline_, nullptr);
        }
        
        if (pipeline_layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        }
        
        if (render_pass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, render_pass_, nullptr);
        }
        
        for (auto image_view : swapchain_image_views_) {
            vkDestroyImageView(device_, image_view, nullptr);
        }
        
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        }
        
        vkDestroyDevice(device_, nullptr);
    }
    
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
#endif

#ifdef PSX5_ENABLE_GLFW
    if (window_) {
        glfwDestroyWindow(window_);
    }
    glfwTerminate();
#endif
    
    initialized_ = false;
}

void VulkanGlfw::present_clear_frame() {
    if (!initialized_) return;
    
#ifdef PSX5_ENABLE_VULKAN
    // Wait for previous frame
    vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
    
    // Acquire next image
    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                           image_available_semaphores_[current_frame_], VK_NULL_HANDLE, &image_index);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain();
        return;
    }
    
    vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);
    
    // Record command buffer
    vkResetCommandBuffer(command_buffers_[current_frame_], 0);
    record_command_buffer(command_buffers_[current_frame_], image_index);
    
    // Submit command buffer
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore wait_semaphores[] = {image_available_semaphores_[current_frame_]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffers_[current_frame_];
    
    VkSemaphore signal_semaphores[] = {render_finished_semaphores_[current_frame_]};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;
    
    if (vkQueueSubmit(graphics_queue_, 1, &submit_info, in_flight_fences_[current_frame_]) != VK_SUCCESS) {
        std::cerr << "Failed to submit draw command buffer!" << std::endl;
    }
    
    // Present
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    
    VkSwapchainKHR swapchains[] = {swapchain_};
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;
    
    result = vkQueuePresentKHR(present_queue_, &present_info);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
    }
    
    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
#endif
}

#ifdef PSX5_ENABLE_VULKAN
bool VulkanGlfw::create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "PS5 Emulator";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "PS5Emu";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;
    
    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    
    uint32_t glfw_extension_count = 0;
    const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    
    create_info.enabledExtensionCount = glfw_extension_count;
    create_info.ppEnabledExtensionNames = glfw_extensions;
    create_info.enabledLayerCount = 0;
    
    return vkCreateInstance(&create_info, nullptr, &instance_) == VK_SUCCESS;
}

bool VulkanGlfw::create_surface() {
    return glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) == VK_SUCCESS;
}

bool VulkanGlfw::pick_physical_device() {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
    
    if (device_count == 0) return false;
    
    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());
    
    for (const auto& device : devices) {
        if (is_device_suitable(device)) {
            physical_device_ = device;
            break;
        }
    }
    
    return physical_device_ != VK_NULL_HANDLE;
}

bool VulkanGlfw::is_device_suitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = find_queue_families(device);
    
    bool extensions_supported = check_device_extension_support(device);
    bool swapchain_adequate = false;
    
    if (extensions_supported) {
        SwapChainSupportDetails swapchain_support = query_swapchain_support(device);
        swapchain_adequate = !swapchain_support.formats.empty() && !swapchain_support.present_modes.empty();
    }
    
    return indices.is_complete() && extensions_supported && swapchain_adequate;
}

// Additional helper methods would continue here...
// For brevity, including key structure but full implementation would be extensive

void VulkanGlfw::record_command_buffer(VkCommandBuffer command_buffer, uint32_t image_index) {
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    
    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
        std::cerr << "Failed to begin recording command buffer!" << std::endl;
        return;
    }
    
    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = render_pass_;
    render_pass_info.framebuffer = swapchain_framebuffers_[image_index];
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = swapchain_extent_;
    
    VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;
    
    vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchain_extent_.width);
    viewport.height = static_cast<float>(swapchain_extent_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapchain_extent_;
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    vkCmdDraw(command_buffer, 3, 1, 0, 0); // Simple triangle, TODO: Implement actual vertex data

    vkCmdEndRenderPass(command_buffer);
    
    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        std::cerr << "Failed to record command buffer!" << std::endl;
    }
}
#endif
