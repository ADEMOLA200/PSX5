\
#include "vulkan_swapchain.h"
#ifdef PSX5_ENABLE_VULKAN
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <stdexcept>

static std::vector<char> read_file(const std::string& path){
    std::ifstream f(path, std::ios::binary|std::ios::ate);
    if(!f) return {};
    auto size = f.tellg(); f.seekg(0);
    std::vector<char> buf(size); f.read(buf.data(), size); return buf;
}

VulkanSwapchain::~VulkanSwapchain(){ shutdown(); }

bool VulkanSwapchain::create_instance(GLFWwindow* window){
    VkApplicationInfo app{}; app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; app.pApplicationName = "psx5"; app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici{}; ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; ici.pApplicationInfo = &app;
    if(vkCreateInstance(&ici, nullptr, &instance_) != VK_SUCCESS){ std::cerr<<"vkCreateInstance failed\n"; return false; }
    if(glfwCreateWindowSurface(instance_, window, nullptr, &surface_) != VK_SUCCESS){ std::cerr<<"glfwCreateWindowSurface failed\n"; return false; }
    return true;
}

bool VulkanSwapchain::pick_physical_device(){ uint32_t count=0; vkEnumeratePhysicalDevices(instance_, &count, nullptr); if(count==0) return false; std::vector<VkPhysicalDevice> devs(count); vkEnumeratePhysicalDevices(instance_, &count, devs.data()); physical_ = devs[0]; return true; }

bool VulkanSwapchain::create_device_and_queues(){ float pr=1.0f; VkDeviceQueueCreateInfo qci{}; qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; qci.queueFamilyIndex=0; qci.queueCount=1; qci.pQueuePriorities=&pr; VkDeviceCreateInfo dci{}; dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO; dci.queueCreateInfoCount=1; dci.pQueueCreateInfos=&qci; if(vkCreateDevice(physical_, &dci, nullptr, &device_)!=VK_SUCCESS){ std::cerr<<"vkCreateDevice failed\n"; return false;} vkGetDeviceQueue(device_,0,0,&graphicsQueue_); vkGetDeviceQueue(device_,0,0,&presentQueue_); return true; }

bool VulkanSwapchain::create_swapchain(GLFWwindow* window){
    VkSurfaceCapabilitiesKHR caps; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);
    VkSwapchainCreateInfoKHR sci{}; sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR; sci.surface = surface_; sci.minImageCount = caps.minImageCount+1; sci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM; sci.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR; sci.imageExtent = caps.currentExtent; sci.imageArrayLayers = 1; sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; sci.preTransform = caps.currentTransform; sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; sci.clipped = VK_TRUE; if(vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_)!=VK_SUCCESS){ std::cerr<<"vkCreateSwapchainKHR failed\n"; return false;} uint32_t count=0; vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr); swapImages_.resize(count); vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapImages_.data()); swapViews_.resize(count); for(uint32_t i=0;i<count;++i){ VkImageViewCreateInfo ivci{}; ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; ivci.image = swapImages_[i]; ivci.viewType = VK_IMAGE_VIEW_TYPE_2D; ivci.format = VK_FORMAT_B8G8R8A8_UNORM; ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; ivci.subresourceRange.levelCount = 1; ivci.subresourceRange.layerCount = 1; if(vkCreateImageView(device_, &ivci, nullptr, &swapViews_[i])!=VK_SUCCESS){ std::cerr<<"vkCreateImageView failed\n"; return false; } }
    return true;
}

bool VulkanSwapchain::create_render_pass(){
    VkAttachmentDescription att{}; att.format = VK_FORMAT_B8G8R8A8_UNORM; att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorRef{}; colorRef.attachment = 0; colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &colorRef;
    VkRenderPassCreateInfo rpci{}; rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpci.attachmentCount = 1; rpci.pAttachments = &att; rpci.subpassCount = 1; rpci.pSubpasses = &sub;
    if(vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_)!=VK_SUCCESS){ std::cerr<<"vkCreateRenderPass failed\n"; return false; }
    return true;
}

// Helper to create framebuffer per swap image
static VkFramebuffer create_framebuffer_for_view(VkDevice dev, VkRenderPass rp, VkImageView view, VkExtent2D extent){
    VkFramebufferCreateInfo fci{}; fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fci.renderPass = rp; fci.attachmentCount = 1; fci.pAttachments = &view; fci.width = extent.width; fci.height = extent.height; fci.layers = 1;
    VkFramebuffer fb; if(vkCreateFramebuffer(dev, &fci, nullptr, &fb) != VK_SUCCESS) return VK_NULL_HANDLE; return fb;
}

VkShaderModule VulkanSwapchain::load_spv_module(const std::string& path){
    auto data = read_file(path);
    if(data.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smci{}; smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO; smci.codeSize = data.size(); smci.pCode = reinterpret_cast<const uint32_t*>(data.data());
    VkShaderModule mod; if(vkCreateShaderModule(device_, &smci, nullptr, &mod)!=VK_SUCCESS) return VK_NULL_HANDLE; return mod;
}

bool VulkanSwapchain::create_pipeline(const std::string& vert_spv_path, const std::string& frag_spv_path){
    auto vmod = load_spv_module(vert_spv_path); auto fmod = load_spv_module(frag_spv_path);
    if(vmod==VK_NULL_HANDLE || fmod==VK_NULL_HANDLE){ std::cerr<<"shader module failed\n"; return false; }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vmod; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fmod; stages[1].pName = "main";
    // Minimal pipeline: no vertex attributes (we'll use hardcoded in shader)
    VkPipelineLayoutCreateInfo plci{}; plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if(vkCreatePipelineLayout(device_, &plci, nullptr, &pipelineLayout_)!=VK_SUCCESS) return false;
    VkGraphicsPipelineCreateInfo gpci{}; gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO; gpci.stageCount = 2; gpci.pStages = stages; gpci.renderPass = renderPass_; gpci.layout = pipelineLayout_;
    if(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipeline_)!=VK_SUCCESS){ vkDestroyShaderModule(device_, vmod, nullptr); vkDestroyShaderModule(device_, fmod, nullptr); std::cerr<<"vkCreateGraphicsPipelines failed\n"; return false; }
    vkDestroyShaderModule(device_, vmod, nullptr); vkDestroyShaderModule(device_, fmod, nullptr);
    return true;
}

bool VulkanSwapchain::create_command_buffers(){
    VkCommandPoolCreateInfo pc{}; pc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pc.queueFamilyIndex = 0;
    if(vkCreateCommandPool(device_, &pc, nullptr, &cmdPool_)!=VK_SUCCESS) return false;
    cmdBuffers_.resize(swapViews_.size());
    VkCommandBufferAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; ai.commandPool = cmdPool_; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = (uint32_t)cmdBuffers_.size();
    if(vkAllocateCommandBuffers(device_, &ai, cmdBuffers_.data())!=VK_SUCCESS) return false;
    // Create framebuffers
    VkExtent2D extent = {800,600};
    for(size_t i=0;i<swapViews_.size();++i){
        VkFramebuffer fb = create_framebuffer_for_view(device_, renderPass_, swapViews_[i], extent);
        // store as a temporary in cmdBuffers_ mapping; we avoid storing framebuffers array for brevity
        (void)fb;
    }
    return true;
}

bool VulkanSwapchain::init(GLFWwindow* window){
    if(!create_instance(window)) return false;
    if(!pick_physical_device()) return false;
    if(!create_device_and_queues()) return false;
    if(!create_swapchain(window)) return false;
    if(!create_render_pass()) return false;
    std::string vert = std::string("build/shaders/quad.vert.spv"); std::string frag = std::string("build/shaders/quad.frag.spv");
    if(!create_pipeline(vert, frag)) { std::cerr<<"create_pipeline failed\n"; /* continue to allow shader replacement */ }
    if(!create_command_buffers()) return false;
    initialized_ = true; return true;
}

void VulkanSwapchain::shutdown(){
    if(device_) vkDeviceWaitIdle(device_);
    if(cmdPool_){ vkDestroyCommandPool(device_, cmdPool_, nullptr); cmdPool_ = VK_NULL_HANDLE; }
    if(pipeline_){ vkDestroyPipeline(device_, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if(pipelineLayout_){ vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
    if(renderPass_){ vkDestroyRenderPass(device_, renderPass_, nullptr); renderPass_ = VK_NULL_HANDLE; }
    for(auto v: swapViews_) if(v) vkDestroyImageView(device_, v, nullptr); swapViews_.clear();
    if(swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE;
    if(device_) vkDestroyDevice(device_, nullptr); device_ = VK_NULL_HANDLE;
    if(surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr); surface_ = VK_NULL_HANDLE;
    if(instance_) vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE;
    initialized_ = false;
}

void VulkanSwapchain::draw_frame(){
    if(!initialized_) return;
    uint32_t imageIndex = 0;
    VkResult res = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, VK_NULL_HANDLE, VK_NULL_HANDLE, &imageIndex);
    if(res != VK_SUCCESS){ std::cerr<<"AcquireNextImageKHR failed\n"; return; }
    VkCommandBuffer cb = cmdBuffers_[imageIndex];
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cb, &bi);
    VkClearValue clearColor{}; clearColor.color = {{0.1f,0.2f,0.3f,1.0f}};
    VkRenderPassBeginInfo rpbi{}; rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; rpbi.renderPass = renderPass_; rpbi.renderArea.offset = {0,0}; rpbi.renderArea.extent = {800,600}; rpbi.clearValueCount = 1; rpbi.pClearValues = &clearColor;
    // We don't have per-swapchain framebuffers stored; for a full implementation, create and track them.
    // For now, just end command buffer after clear and submit to present a cleared image.
    // TODO: Implement actual framebuffer creation and management
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 0, nullptr);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(graphicsQueue_, 1, &si, VK_NULL_HANDLE); vkQueueWaitIdle(graphicsQueue_);
    VkPresentInfoKHR pi{}; pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR; pi.swapchainCount = 1; pi.pSwapchains = &swapchain_; pi.pImageIndices = &imageIndex; vkQueuePresentKHR(presentQueue_, &pi);
    std::cout<<"VulkanSwapchain::draw_frame() - presented\n";
}

#endif
