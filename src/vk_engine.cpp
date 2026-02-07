#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_types.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include "VkBootstrap.h" 

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

void VulkanEngine::cleanup()
{
    if (isInitialized) { // avoid unfinished init makes destroy crush
        vkDeviceWaitIdle(_device);

        for (auto& frame : _frames) {
            frame._deletionQueue.flush();
        }

        if (_skyboxImage.image != VK_NULL_HANDLE) {
            vkDestroyImageView(_device, _skyboxImage.imageView, nullptr);
            vkDestroyImage(_device, _skyboxImage.image, nullptr);
            vkFreeMemory(_device, _skyboxImage.memory, nullptr);
            _skyboxImage.image = VK_NULL_HANDLE;
        }

        _mainDeletionQueue.flush();
        _materialSystem.cleanup();

        destroy_swapchain(); // use when resize and cleanup

        vkDestroySurfaceKHR(_instance, _surface, nullptr);

        vmaDestroyAllocator(_allocator);

        vkDestroyDevice(_device, nullptr);
        vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
        vkDestroyInstance(_instance, nullptr);

        SDL_DestroyWindow(_window);
    }
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
    _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    vkb::SwapchainBuilder swapchain_builder{ _physical_device, _device, _surface };
    vkb::Swapchain vkb_swapchain = swapchain_builder
        .set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }) // TODO:automatically set?
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR) // VSync
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT) // used when draw image copy to swapchain, default VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
        // .set_old_swapchain(_swapchain) // TODO:use set_old_swapchain to avoid resize flush
        .build()
        .value();
    _swapchain = vkb_swapchain.swapchain;
    _swapchainImages = vkb_swapchain.get_images().value();
    _swapchainImageViews = vkb_swapchain.get_image_views().value();
    _swapchainExtent = vkb_swapchain.extent;
}

void VulkanEngine::destroy_swapchain()
{
    vkDeviceWaitIdle(_device);

    if (_drawImage.image != VK_NULL_HANDLE) {
        vkDestroyImageView(_device, _drawImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
        _drawImage.image = VK_NULL_HANDLE;
        _drawImage.imageView = VK_NULL_HANDLE;
    } 
    if (_depthImage.image != VK_NULL_HANDLE) {
        vkDestroyImageView(_device, _depthImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
        _depthImage.image = VK_NULL_HANDLE;
        _depthImage.imageView = VK_NULL_HANDLE;
    }
    if (_postprocessImage.image != VK_NULL_HANDLE) {
        vkDestroyImageView(_device, _postprocessImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _postprocessImage.image, _postprocessImage.allocation);
        _postprocessImage.image = VK_NULL_HANDLE;
        _postprocessImage.imageView = VK_NULL_HANDLE;
    }
    if (_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(_device, _swapchain, nullptr); // _swapchainImage is subresource of _swapchain, automatically destroy
        _swapchain = VK_NULL_HANDLE;
    }
    for (int i = 0; i < _swapchainImageViews.size(); i++) {
        vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
    }
    _swapchainImageViews.clear();
}

void VulkanEngine::resize_swapchain()
{
    vkDeviceWaitIdle(_device);

    int w, h;
    SDL_GetWindowSize(_window, &w, &h);
    if (w <= 0 || h <= 0) return;
    _windowExtent.width = w;
    _windowExtent.height = h;

    init_swapchain();

    // update the address of image view for descriptor set
    if (_drawImageDescriptorLayout != VK_NULL_HANDLE) {
        DescriptorWriter writer;
        writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer.update_set(_device, _drawImageDescriptorSet);      
    }

    // update postprocess descriptor set
    if (_postprocessDescriptorSetLayout != VK_NULL_HANDLE) {
        DescriptorWriter writer0;
        writer0.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer0.write_image(1, _postprocessImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer0.update_set(_device, _postprocessDescriptorSets[0]);

        DescriptorWriter writer1;
        writer1.write_image(0, _postprocessImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer1.write_image(1, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer1.update_set(_device, _postprocessDescriptorSets[1]);      
    }

    resize_requested = false;
}