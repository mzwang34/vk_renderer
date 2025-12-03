#include "vk_initializers.h"

VkImageCreateInfo vkinit::image_create_info(VkFormat format,  VkExtent3D extent, VkImageUsageFlags usageFlags)
{
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = nullptr;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = extent;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usageFlags;

    return imageInfo;
}

VkImageViewCreateInfo vkinit::imageview_create_info(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo imageViewInfo {};
    imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewInfo.pNext = nullptr;
    imageViewInfo.image = image;
    imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewInfo.format = format;
    imageViewInfo.subresourceRange = {
        .aspectMask = aspectFlags,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    return imageViewInfo;
}

VkCommandPoolCreateInfo vkinit::command_pool_create_info(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags)
{
    VkCommandPoolCreateInfo commandPoolInfo {};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.pNext = nullptr;
    commandPoolInfo.queueFamilyIndex = queueFamilyIndex;
    commandPoolInfo.flags = flags;

    return commandPoolInfo;
}

VkCommandBufferAllocateInfo vkinit::command_buffer_allocate_info(VkCommandPool commandPool, VkCommandBufferLevel level, uint32_t count)
{
    VkCommandBufferAllocateInfo commandBufferAllocateInfo {};
    commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.pNext = nullptr;
    commandBufferAllocateInfo.commandPool = commandPool;
    commandBufferAllocateInfo.level = level;
    commandBufferAllocateInfo.commandBufferCount = count;

    return commandBufferAllocateInfo;
}

VkFenceCreateInfo vkinit::fence_create_info(VkFenceCreateFlags flags)
{
    VkFenceCreateInfo fenceInfo {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.pNext = nullptr;
    fenceInfo.flags = flags;

    return fenceInfo;
}

VkSemaphoreCreateInfo vkinit::semaphore_create_info(VkSemaphoreCreateFlags flags)
{
    VkSemaphoreCreateInfo semaphoreInfo {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = nullptr;
    semaphoreInfo.flags = flags;

    return semaphoreInfo;
}

VkCommandBufferBeginInfo vkinit::command_buffer_begin_info(VkCommandBufferUsageFlags flags)
{
    VkCommandBufferBeginInfo cmdBufBeginInfo {};
    cmdBufBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufBeginInfo.pNext = nullptr;
    cmdBufBeginInfo.pInheritanceInfo = nullptr;
    cmdBufBeginInfo.flags = flags;

    return cmdBufBeginInfo;
}

VkCommandBufferSubmitInfo vkinit::command_buffer_submit_info(VkCommandBuffer cmd)
{
    VkCommandBufferSubmitInfo cmdBufSubmitInfo {};
    cmdBufSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdBufSubmitInfo.pNext = nullptr;
    cmdBufSubmitInfo.commandBuffer = cmd;
    cmdBufSubmitInfo.deviceMask = 0;

    return cmdBufSubmitInfo;
}

VkSemaphoreSubmitInfo vkinit::semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore)
{
    VkSemaphoreSubmitInfo semaphoreSubmitInfo {};
    semaphoreSubmitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    semaphoreSubmitInfo.pNext = nullptr;
    semaphoreSubmitInfo.semaphore = semaphore;
    semaphoreSubmitInfo.stageMask = stageMask;
    semaphoreSubmitInfo.deviceIndex = 0;
    semaphoreSubmitInfo.value = 1;

    return semaphoreSubmitInfo;
}
    
VkSubmitInfo2 vkinit::submit_info(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitSemaphoreInfo)
{
    VkSubmitInfo2 submitInfo {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.pNext = nullptr;
    submitInfo.waitSemaphoreInfoCount = waitSemaphoreInfo == nullptr ? 0 : 1;
    submitInfo.pWaitSemaphoreInfos = waitSemaphoreInfo;
    submitInfo.signalSemaphoreInfoCount = signalSemaphoreInfo == nullptr ? 0 : 1;
    submitInfo.pSignalSemaphoreInfos = signalSemaphoreInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = cmd;

    return submitInfo;
}
    
VkPresentInfoKHR vkinit::present_info()
{
    VkPresentInfoKHR presentInfo {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.swapchainCount = 0;
    presentInfo.pSwapchains = nullptr;
    presentInfo.pWaitSemaphores = nullptr;
    presentInfo.waitSemaphoreCount = 0;
    presentInfo.pImageIndices = nullptr;

    return presentInfo;
}

VkImageSubresourceRange vkinit::image_subresource_range(VkImageAspectFlags aspectMask)
{
    VkImageSubresourceRange subImage{};
    subImage.aspectMask = aspectMask;
    subImage.baseMipLevel = 0;
    subImage.levelCount = VK_REMAINING_MIP_LEVELS;
    subImage.baseArrayLayer = 0;
    subImage.layerCount = VK_REMAINING_ARRAY_LAYERS;

    return subImage;
}

VkRenderingAttachmentInfo vkinit::attachment_info(VkImageView imageview, VkClearValue* clear, VkImageLayout layout)
{
    VkRenderingAttachmentInfo attachmentInfo {};
    attachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachmentInfo.pNext = nullptr;
    attachmentInfo.imageView = imageview;
    attachmentInfo.imageLayout = layout;
    attachmentInfo.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    attachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (clear) attachmentInfo.clearValue = *clear;

    return attachmentInfo;
}

VkRenderingInfo vkinit::rendering_info(VkExtent2D renderExtent, VkRenderingAttachmentInfo* colorAttachmentInfo, VkRenderingAttachmentInfo* depthAttachmentInfo)
{
    VkRenderingInfo renderingInfo {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.pNext = nullptr;
    renderingInfo.renderArea = VkRect2D{ VkOffset2D{0, 0}, renderExtent };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = colorAttachmentInfo;
    renderingInfo.pDepthAttachment = depthAttachmentInfo;
    renderingInfo.pStencilAttachment = nullptr;

    return renderingInfo;
}