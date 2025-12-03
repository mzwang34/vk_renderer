#pragma once

#include "vk_types.h"
#include "vk_descriptors.h"

#include <deque>

constexpr unsigned int FRAME_OVERLAP = 2;

struct FrameData {
    VkCommandPool _commandPool;
    VkCommandBuffer _commandBuffer;

    DeletionQueue _deletionQueue;

    VkFence _renderFence;
    VkSemaphore _swapchainSemaphore;
    VkSemaphore _renderSemaphore;

    DescriptorAllocatorGrowable _frameDescriptorAllocator;
};  

class VulkanEngine {
public:
    bool isInitialized {false};
    bool resize_requested { false };
    bool freeze_rendering { false };
    int _frameNumber { 0 };

    struct SDL_Window* _window {nullptr};
    VkExtent2D _windowExtent {1280, 720};

    bool bUseValidationLayers = false;
    VkInstance _instance;
    VkDebugUtilsMessengerEXT _debug_messenger;
    VkSurfaceKHR _surface;
    VkPhysicalDevice _physical_device;
    VkDevice _device;
    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;
    VmaAllocator _allocator;

    AllocatedImage _drawImage;
    AllocatedImage _depthImage;

    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    VkFormat _swapchainImageFormat;
    VkExtent2D _swapchainExtent;
    VkExtent2D _drawExtent;

    FrameData _frames[FRAME_OVERLAP];

    DeletionQueue _mainDeletionQueue;

    VkCommandPool _immCommandPool;
    VkCommandBuffer _immCommandBuffer;
    VkFence _immFence;

    DescriptorAllocatorGrowable _globalDescriptorAllocator;

    VkDescriptorSet _drawImageDescriptorSet;
    VkDescriptorSetLayout _drawImageDescriptorLayout; 
    VkDescriptorSetLayout _globalSceneDescriptorLayout;

    VkPipeline _gradientPipeline;
    VkPipelineLayout _gradientPipelineLayout;
    std::vector<ComputeEffect> backgroundEffects;

    EngineStats stats;

    float _frameTimeAccumulator = 0.f;
    int _frameCountAccumulator = 0;

    void init();
    void run();
    void cleanup();

    FrameData& get_current_frame();
    
private:
    void init_window();
    void init_vulkan();
    void init_swapchain();
    void init_commands();
    void init_sync();
    void init_descriptors();
    void init_pipelines();
    void init_scene();
    void init_imgui();

    void create_swapchain(uint32_t width, uint32_t height);
    void resize_swapchain();
    void destroy_swapchain();

    void draw();
    void draw_background(VkCommandBuffer cmd);
    void draw_geometry(VkCommandBuffer cmd);
    void draw_postprocess(VkCommandBuffer cmd);
    void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);

    void run_imgui();
};