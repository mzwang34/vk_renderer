#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_types.h"
#include "vk_descriptors.h"
#include "vk_pipelines.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include "VkBootstrap.h" 

// #define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <array>

void VulkanEngine::init()
{
    init_window();
    init_vulkan();
    init_swapchain();
    init_commands();
    init_sync();
    init_descriptors();
    init_pipelines();
    init_scene();
    init_imgui();

    isInitialized = true;
}


void VulkanEngine::init_window()
{
    SDL_Init(SDL_INIT_VIDEO);
    _window = SDL_CreateWindow("bamboo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, _windowExtent.width, _windowExtent.height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
}

// instance, surface, physical device, device, queue, allocator
void VulkanEngine::init_vulkan()
{
    // init instance
    vkb::InstanceBuilder instance_builder;
    auto instance_ret = instance_builder.use_default_debug_messenger().request_validation_layers(bUseValidationLayers).build();
    vkb::Instance vkb_instance = instance_ret.value();
    _instance = vkb_instance.instance;
    _debug_messenger = vkb_instance.debug_messenger;

    // init surface
    SDL_Vulkan_CreateSurface( _window, _instance, &_surface );

    // init physical device
    VkPhysicalDeviceVulkan13Features features13 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES }; // select for physical device and enable in logical device
    features13.dynamicRendering = true;
    features13.synchronization2 = true;

    VkPhysicalDeviceVulkan12Features features12 { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;
    features12.descriptorBindingPartiallyBound = true;
    features12.descriptorBindingVariableDescriptorCount = true;
    features12.runtimeDescriptorArray = true;

    vkb::PhysicalDeviceSelector phys_device_selector{ vkb_instance };
    auto phys_device_ret = phys_device_selector.set_minimum_version(1, 3).set_required_features_13(features13).set_required_features_12(features12).set_surface(_surface).select();
    vkb::PhysicalDevice vkb_physical_device = phys_device_ret.value();
    _physical_device = vkb_physical_device.physical_device;

    // init device
    vkb::DeviceBuilder device_builder{ vkb_physical_device };
    auto device_ret = device_builder.build();
    vkb::Device vkb_device = device_ret.value();
    _device = vkb_device.device;

    // init queue
    _graphicsQueue = vkb_device.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    // init memory allocator
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = _physical_device;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &_allocator);
}

void VulkanEngine::init_swapchain()
{
    destroy_swapchain();
    // init swapchain and its image, imageview
    create_swapchain(_windowExtent.width, _windowExtent.height);

    // init offscreen draw image and imageview
    // draw image
    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _drawImage.imageExtent = { _windowExtent.width, _windowExtent.height, 1 };
    VkImageUsageFlags drawImageUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // blit to swapchain, compute shader, render target
    VkImageCreateInfo drawImageInfo = vkinit::image_create_info(_drawImage.imageFormat, _drawImage.imageExtent, drawImageUsageFlags);

    VmaAllocationCreateInfo drawImageAllocationInfo = {};
    drawImageAllocationInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    drawImageAllocationInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); // fast GPU VRAM

    vmaCreateImage(_allocator, &drawImageInfo, &drawImageAllocationInfo, &_drawImage.image, &_drawImage.allocation, nullptr);

    VkImageViewCreateInfo drawImageViewInfo = vkinit::imageview_create_info(_drawImage.image, _drawImage.imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(_device, &drawImageViewInfo, nullptr, &_drawImage.imageView)); // image view has no GPU memory so no need vma

    // depth image
    _depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _depthImage.imageExtent = _drawImage.imageExtent;
    VkImageUsageFlags depthImageUsageFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    VkImageCreateInfo depthImageInfo = vkinit::image_create_info(_depthImage.imageFormat, _depthImage.imageExtent, depthImageUsageFlags);
    vmaCreateImage(_allocator, &depthImageInfo, &drawImageAllocationInfo, &_depthImage.image, &_depthImage.allocation, nullptr);

    VkImageViewCreateInfo depthImageViewInfo = vkinit::imageview_create_info(_depthImage.image, _depthImage.imageFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    VK_CHECK(vkCreateImageView(_device, &depthImageViewInfo, nullptr, &_depthImage.imageView));
}

// init command pool and command buffer
void VulkanEngine::init_commands()
{
    VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool)); // if use same command pool for all frames, resetting frame0 also delete commands of frame1
        VkCommandBufferAllocateInfo cmdBufAllocteInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdBufAllocteInfo, &_frames[i]._commandBuffer));

        // destroy command pool at the final cleanup, reset for each frame rather destroy it
        // destroy command pool will also destroy command buffer
        _mainDeletionQueue.push_function([=]() {vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr); }); 
    }

    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool)); 
    VkCommandBufferAllocateInfo cmdBufAllocteInfo = vkinit::command_buffer_allocate_info(_immCommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdBufAllocteInfo, &_immCommandBuffer));

    _mainDeletionQueue.push_function([=]() {vkDestroyCommandPool(_device, _immCommandPool, nullptr); }); 
}

// init fence and semaphore
void VulkanEngine::init_sync()
{
    // fence (gpu <-> cpu): wait gpu finished last frame
    // semaphore (gpu <-> gpu): swapchain semaphore wait until image ready, then start rendering, render semaphore wai until render finished to the final present
    VkFenceCreateInfo fenceInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT); // initially set signaled to avoid block at first
    VK_CHECK(vkCreateFence(_device, &fenceInfo, nullptr, &_immFence)); // immediate command doesn't need semaphore
    _mainDeletionQueue.push_function([=]() { vkDestroyFence(_device, _immFence, nullptr); });

    for (int i = 0; i < FRAME_OVERLAP; i++) {
        VK_CHECK(vkCreateFence(_device, &fenceInfo, nullptr, &_frames[i]._renderFence));

        VkSemaphoreCreateInfo semaphoreInfo = vkinit::semaphore_create_info(0);
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &_frames[i]._swapchainSemaphore));
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreInfo, nullptr, &_frames[i]._renderSemaphore));

        _mainDeletionQueue.push_function([=]() { 
            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr); 
            vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
        });
    }
}

// init descriptor pool, set layout and descriptor set
void VulkanEngine::init_descriptors()
{
    // init global descriptor pool
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
    };

    _globalDescriptorAllocator.init(_device, 10, sizes);
    _mainDeletionQueue.push_function([&]() { _globalDescriptorAllocator.destroy_pools(_device); });

    // init descriptor set layout
    // layout 0: compute shader
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        _drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    // layout 1: graphics shader
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        std::array<VkDescriptorBindingFlags, 2> flagArray = {
            0, // ubo
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT}; // variable descriptor count, sampler
        builder.bindings[1].descriptorCount = 4048; // max descriptors

        VkDescriptorSetLayoutBindingFlagsCreateInfo bindFlags = { 
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
            .pNext = nullptr };
        
        bindFlags.bindingCount = 2;
        bindFlags.pBindingFlags = flagArray.data();

        _globalSceneDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &bindFlags);
    }

    _mainDeletionQueue.push_function([&]() {
        vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _globalSceneDescriptorLayout, nullptr);
    });

    _drawImageDescriptorSet = _globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);
    DescriptorWriter writer;
    writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.update_set(_device, _drawImageDescriptorSet);

    // init per-frame descriptor pool
    for (int i = 0; i < FRAME_OVERLAP; i++) {
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
		};

        _frames[i]._frameDescriptorAllocator = DescriptorAllocatorGrowable{};
        _frames[i]._frameDescriptorAllocator.init(_device, 1000, frame_sizes);
        _mainDeletionQueue.push_function([&, i]() {_frames[i]._frameDescriptorAllocator.destroy_pools(_device); });
    }
}

// init pipeline layout, shader module, pipeline
void VulkanEngine::init_pipelines()
{
    // init pipeline layout
    VkPipelineLayoutCreateInfo computeLayout{};
	computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext = nullptr;
	computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
	computeLayout.setLayoutCount = 1;

	VkPushConstantRange pushConstant{};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(ComputePushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges = &pushConstant;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

    // init shader module
	VkShaderModule gradientShader;
	if (!vkutil::load_shader_module("../../shaders/gradient_color.comp.spv", _device, &gradientShader)) {
		fmt::print("Error when building the compute shader \n");
	}

    VkPipelineShaderStageCreateInfo stageinfo{};
	stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageinfo.pNext = nullptr;
	stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageinfo.module = gradientShader;
	stageinfo.pName = "main";

    // init pipeline
    VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = _gradientPipelineLayout;
	computePipelineCreateInfo.stage = stageinfo;

	ComputeEffect gradient;
	gradient.layout = _gradientPipelineLayout;
	gradient.name = "gradient";
	gradient.data = {};
    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
	gradient.data.data2 = glm::vec4(0, 0, 1, 1);

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline));

    backgroundEffects.push_back(gradient);
    vkDestroyShaderModule(_device, gradientShader, nullptr);
	_mainDeletionQueue.push_function([&]() {
		vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
		vkDestroyPipeline(_device, gradient.pipeline, nullptr);
		});
}

void VulkanEngine::init_scene()
{

}

void VulkanEngine::init_imgui()
{
    
}