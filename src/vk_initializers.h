#pragma once

#include "vk_types.h"

namespace vkinit {
    VkImageCreateInfo image_create_info(VkFormat format, VkExtent3D extent, VkImageUsageFlags usageFlags);
    VkImageViewCreateInfo imageview_create_info(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

    VkCommandPoolCreateInfo command_pool_create_info(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags);
    VkCommandBufferAllocateInfo command_buffer_allocate_info(VkCommandPool commandPool, VkCommandBufferLevel level, uint32_t count);

    VkFenceCreateInfo fence_create_info(VkFenceCreateFlags flags);
    VkSemaphoreCreateInfo semaphore_create_info(VkSemaphoreCreateFlags flags);

    VkCommandBufferBeginInfo command_buffer_begin_info(VkCommandBufferUsageFlags flags = 0);

    VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd);
    VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore);
    VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitSemaphoreInfo);
    VkPresentInfoKHR present_info();

    VkImageSubresourceRange image_subresource_range(VkImageAspectFlags aspectMask);

    VkRenderingAttachmentInfo attachment_info(VkImageView imageview, VkClearValue* clear, VkImageLayout layout);
    VkRenderingAttachmentInfo depth_attachment_info(VkImageView view, VkImageLayout layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    VkRenderingInfo rendering_info(VkExtent2D renderExtent, VkRenderingAttachmentInfo* colorAttachmentInfo, VkRenderingAttachmentInfo* depthAttachmentInfo);

    VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_info(VkShaderStageFlagBits stage, VkShaderModule shaderModule, const char* entry = "main");
    VkPipelineLayoutCreateInfo pipeline_layout_create_info();
}