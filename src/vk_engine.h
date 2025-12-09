#pragma once

#include "vk_types.h"
#include "vk_descriptors.h"
#include "vk_materials.h"
#include "vk_camera.h"

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

    bool bUseValidationLayers = true;
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
    Camera _mainCamera;

    std::shared_ptr<Node> _sceneRoot;
    std::vector<RenderObject> _renderObjects;

    MaterialSystem _materialSystem;
    std::unordered_map<std::string, std::shared_ptr<MeshAsset>> _meshAssets;

    std::shared_ptr<AllocatedImage> _whiteTexture;
    std::shared_ptr<AllocatedImage> _blackTexture;
    std::shared_ptr<AllocatedImage> _errorCheckerboardImage;
    std::shared_ptr<AllocatedImage> _defaultNormalTexture;

    VkSampler _defaultSamplerLinear;
    VkSampler _defaultSamplerNearest;

    std::vector<std::shared_ptr<AllocatedImage>> _loadedImages;

    float _frameTimeAccumulator = 0.f;
    int _frameCountAccumulator = 0;

    void init();
    void run();
    void cleanup();

    FrameData& get_current_frame();

    std::shared_ptr<Node> load_gltf(std::string name, std::string fileName);
    std::shared_ptr<AllocatedImage> create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    void destroy_buffer(const AllocatedBuffer& buffer);
    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);
    AllocatedBuffer upload_mesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
    
private:
    void init_window();
    void init_vulkan();
    void init_swapchain();
    void init_commands();
    void init_sync();
    void init_descriptors();
    void init_default_data();

    void init_pipelines();
    void init_background_pipelines();
    void init_mesh_pipelines();
    
    void init_scene();
    void init_imgui();
    void init_camera();

    void create_swapchain(uint32_t width, uint32_t height);
    void resize_swapchain();
    void destroy_swapchain();

    void draw();
    void draw_background(VkCommandBuffer cmd);
    void draw_geometry(VkCommandBuffer cmd);
    void draw_postprocess(VkCommandBuffer cmd);
    void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);
    void update_scene(float dt);

    void run_imgui();
};