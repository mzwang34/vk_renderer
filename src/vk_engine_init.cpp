#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_types.h"
#include "vk_descriptors.h"
#include "vk_pipelines.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include "VkBootstrap.h" 

#include "vk_mem_alloc.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include <array>

void VulkanEngine::init()
{
    init_window();
    init_vulkan();
    init_swapchain();
    init_commands();
    init_sync();
    init_descriptors();
    init_default_data();
    init_shadow_resources();
    init_pipelines();
    init_scene();
    init_imgui();
    init_camera();

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
    auto instance_ret = instance_builder.use_default_debug_messenger().request_validation_layers(bUseValidationLayers).require_api_version(1, 3).build();
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
    features12.descriptorBindingPartiallyBound = true;
    features12.descriptorBindingVariableDescriptorCount = true;
    features12.runtimeDescriptorArray = true;
    features12.scalarBlockLayout = true;
    features12.shaderSampledImageArrayNonUniformIndexing = true;
    features12.descriptorBindingSampledImageUpdateAfterBind = true;

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
    // compute shader layout
    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        _drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
    }

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _globalSceneDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    // bindless texture layout
    {
        DescriptorLayoutBuilder builder;
        VkDescriptorBindingFlags bindFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extendedInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT,
            .bindingCount = 1,
            .pBindingFlags = &bindFlags,
        };

        builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        builder.bindings[0].descriptorCount = 4096;
        builder.bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

        _bindlessTextureLayout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, &extendedInfo, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT);
    }

    _mainDeletionQueue.push_function([&]() {
        vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _globalSceneDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _bindlessTextureLayout, nullptr);
    });

    {
        // create a descriptor pool for UPDATE_AFTER_BIND, allow texture update at any time
        VkDescriptorPoolSize poolSizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 } };
        VkDescriptorPoolCreateInfo poolInfo {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = poolSizes;
        
        VkDescriptorPool bindlessPool;
        VK_CHECK(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &bindlessPool));
        _mainDeletionQueue.push_function([=]() { vkDestroyDescriptorPool(_device, bindlessPool, nullptr); });

        uint32_t maxBinding = 4096;
        VkDescriptorSetVariableDescriptorCountAllocateInfoEXT countInfo {};
        countInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT;
        countInfo.descriptorSetCount = 1;
        countInfo.pDescriptorCounts = &maxBinding;

        VkDescriptorSetAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.pNext = &countInfo;
        allocInfo.descriptorPool = bindlessPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &_bindlessTextureLayout;

        VK_CHECK(vkAllocateDescriptorSets(_device, &allocInfo, &_bindlessDescriptorSet));
    }

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

// init default texture
void VulkanEngine::init_default_data()
{
    uint32_t white = 0xFFFFFFFF; 
    _whiteTexture = create_image((void*)&white, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false); 

    uint32_t black = 0xFF000000;
    _blackTexture = create_image((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);

    uint32_t normalDefault = 0xFFFF8080; 
    _defaultNormalTexture = create_image((void*)&normalDefault, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);

    int checkSize = 16;
    std::vector<uint32_t> pixels(checkSize * checkSize);
    
    for (int x = 0; x < checkSize; x++) {
        for (int y = 0; y < checkSize; y++) {
            uint32_t magenta = 0xFFFF00FF;
            uint32_t blackColor = 0xFF000000;
            
            pixels[y * checkSize + x] = ((x % 2) ^ (y % 2)) ? magenta : blackColor;
        }
    }
    
    _errorCheckerboardImage = create_image(pixels.data(), VkExtent3D{ (uint32_t)checkSize, (uint32_t)checkSize, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);

    VkSamplerCreateInfo sampl = vkinit::sampler_create_info(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerLinear);

    sampl.magFilter = VK_FILTER_NEAREST;
    sampl.minFilter = VK_FILTER_NEAREST;
    vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerNearest);

    // white texture for default base color texture and metal-rough texture
    update_bindless_texture(0, _whiteTexture->imageView, _defaultSamplerNearest);
    _globalTextureIndex = 1;
    // default normal texture
    update_bindless_texture(0, _defaultNormalTexture->imageView, _defaultSamplerNearest);
    _globalTextureIndex = 2;

    _mainDeletionQueue.push_function([=](){
        vkDestroySampler(_device, _defaultSamplerLinear, nullptr);
        vkDestroySampler(_device, _defaultSamplerNearest, nullptr);
    });
}

void VulkanEngine::init_shadow_resources()
{
    // create shadow image and image view
    _shadowImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _shadowImage.imageExtent = { _shadowExtent.width, _shadowExtent.height, 1};
    VkImageUsageFlags shadowUsageFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageCreateInfo shadowImageInfo = vkinit::image_create_info(_shadowImage.imageFormat, _shadowImage.imageExtent, shadowUsageFlags);
    
    VmaAllocationCreateInfo shadowImageAllocationInfo = {};
    shadowImageAllocationInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    shadowImageAllocationInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); // fast GPU VRAM
    vmaCreateImage(_allocator, &shadowImageInfo, &shadowImageAllocationInfo, &_shadowImage.image, &_shadowImage.allocation, nullptr);

    VkImageViewCreateInfo shadowImageViewInfo = vkinit::imageview_create_info(_shadowImage.image, _shadowImage.imageFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    VK_CHECK(vkCreateImageView(_device, &shadowImageViewInfo, nullptr, &_shadowImage.imageView));

    // create shadow sampler
    VkSamplerCreateInfo shadowSamplerInfo = vkinit::sampler_create_info(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    VK_CHECK(vkCreateSampler(_device, &shadowSamplerInfo, nullptr, &_shadowSampler));

    _mainDeletionQueue.push_function([=]() {
        vkDestroySampler(_device, _shadowSampler, nullptr);
        vkDestroyImageView(_device, _shadowImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _shadowImage.image, _shadowImage.allocation);
    });
}

void VulkanEngine::init_pipelines() 
{
    init_shadow_pipeline();
    init_background_pipelines();
    init_mesh_pipelines();
}

void VulkanEngine::init_shadow_pipeline()
{
    // init pipeline layout
    VkPipelineLayoutCreateInfo shadowPipelineLayoutInfo = vkinit::pipeline_layout_create_info();
    shadowPipelineLayoutInfo.setLayoutCount = 1;
    shadowPipelineLayoutInfo.pSetLayouts = &_globalSceneDescriptorLayout;

    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(GPUDrawPushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    shadowPipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    shadowPipelineLayoutInfo.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(_device, &shadowPipelineLayoutInfo, nullptr, &_shadowPipelineLayout));

    // load shader module
    VkShaderModule shadowShader;
    vkutil::load_shader_module("../../shaders/shadow.vert.spv", _device, &shadowShader);

    // init pipeline
    PipelineBuilder pipelineBuilder;
    pipelineBuilder.clear();
    pipelineBuilder._pipelineLayout = _shadowPipelineLayout;
    pipelineBuilder.set_shaders(shadowShader, VK_NULL_HANDLE);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE); // solve peter panning
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
    pipelineBuilder.set_depth_format(_shadowImage.imageFormat);
    pipelineBuilder._renderInfo.colorAttachmentCount = 0;
    pipelineBuilder._renderInfo.pColorAttachmentFormats = nullptr;

    _shadowPipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, shadowShader, nullptr);
    _mainDeletionQueue.push_function([=]() {
        vkDestroyPipelineLayout(_device, _shadowPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _shadowPipeline, nullptr);
    });
}

void VulkanEngine::init_background_pipelines()
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

    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_gradientPipeline));

	ComputeEffect gradient;
	gradient.layout = _gradientPipelineLayout;
	gradient.name = "gradient";
    gradient.pipeline = _gradientPipeline;
	gradient.data = {};
    gradient.data.data1 = glm::vec4(1, 0, 0, 1);
	gradient.data.data2 = glm::vec4(0, 0, 1, 1);

    backgroundEffects.push_back(gradient);

    vkDestroyShaderModule(_device, gradientShader, nullptr);
	_mainDeletionQueue.push_function([&]() {
		vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _gradientPipeline, nullptr);
		});
}

void VulkanEngine::init_mesh_pipelines()
{
    _materialSystem.init(this);

    DescriptorLayoutBuilder matLayoutBuilder;
    matLayoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER); // constants, uniform buffer
    VkDescriptorSetLayout matLayout = matLayoutBuilder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    _mainDeletionQueue.push_function([&, matLayout]() {
        vkDestroyDescriptorSetLayout(_device, matLayout, nullptr);
    });

    VkShaderModule meshVertShader, meshFragShader;
    vkutil::load_shader_module("../../shaders/mesh.vert.spv", _device, &meshVertShader);
    vkutil::load_shader_module("../../shaders/mesh.frag.spv", _device, &meshFragShader);


    PipelineBuilder pipelineBuilder;
    pipelineBuilder.set_shaders(meshVertShader, meshFragShader);
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
    pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(_depthImage.imageFormat);
    
    _materialSystem.register_template("Opaque", pipelineBuilder, matLayout, MaterialPass::MainColor);

    pipelineBuilder.enable_blending_additive();
    pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_LESS_OR_EQUAL);
    _materialSystem.register_template("Transparent", pipelineBuilder, matLayout, MaterialPass::Transparent);


    vkDestroyShaderModule(_device, meshVertShader, nullptr);
    vkDestroyShaderModule(_device, meshFragShader, nullptr);
}

void VulkanEngine::init_scene()
{
    // init Node
    _sceneRoot = std::make_shared<Node>();
    _sceneRoot->localTransform = glm::mat4(1.f);

    // load mesh assets
    auto structureNode = load_gltf("structure", "../../assets/house.glb");
    _sceneRoot->addChild(structureNode);
    // auto prototypeNode = load_gltf("structure", "../../assets/basicmesh.glb");
    std::function<std::shared_ptr<Node>(std::shared_ptr<Node>)> cloneNode = 
        [&](std::shared_ptr<Node> source) -> std::shared_ptr<Node> {
        std::shared_ptr<Node> newNode = std::make_shared<Node>();
        newNode->localTransform = source->localTransform; 
        newNode->mesh = source->mesh;
        
        for (auto& child : source->children) {
            newNode->addChild(cloneNode(child));
        }
        return newNode;
    };

    // multiple instances test
    // int gridCount = 10;
    // float distance = 10.0f;
    // for (int x = -gridCount; x < gridCount; x++) {
    //     for (int z = -gridCount; z < gridCount; z++) {
    //         std::shared_ptr<Node> newNode = cloneNode(prototypeNode);
    //         glm::mat4 translation = glm::translate(glm::mat4(1.f), glm::vec3(x * distance, 0, z * distance));
    //         newNode->localTransform = translation;
    //         _sceneRoot->addChild(newNode);
    //     }
    // }
}

void VulkanEngine::init_imgui()
{
    // init descriptor pool
    VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };
    
    VkDescriptorPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.pNext = nullptr;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = (uint32_t)std::size(pool_sizes);
    poolInfo.pPoolSizes = pool_sizes;
    
    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &imguiPool));

    // init imgui library
    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForVulkan(_window);

    ImGui_ImplVulkan_InitInfo initInfo {};
    initInfo.Instance = _instance;
    initInfo.PhysicalDevice = _physical_device;
    initInfo.Device = _device;
    initInfo.Queue = _graphicsQueue;
    initInfo.DescriptorPool = imguiPool;
    initInfo.MinImageCount = 3; // swapchain image count
    initInfo.ImageCount = 3;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);
    ImGui_ImplVulkan_CreateFontsTexture();

    _mainDeletionQueue.push_function([=]() {
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(_device, imguiPool, nullptr);
    });
}

void VulkanEngine::init_camera()
{
    _mainCamera.velocity = glm::vec3(0.f);
    _mainCamera.position = glm::vec3(0.f);

    _mainCamera.pitch = 0;
    _mainCamera.yaw = 0;
}