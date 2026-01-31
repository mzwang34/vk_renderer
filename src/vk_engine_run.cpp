#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_images.h"
#include "vk_types.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include "VkBootstrap.h" 
#include <thread>

#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"
#include <cmath>
#include <algorithm>

void VulkanEngine::run()
{
    bool done = false;
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (!done) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float, std::chrono::milliseconds::period>(currentTime - lastTime).count();
        lastTime = currentTime;

        _frameTimeAccumulator += dt;
        _frameCountAccumulator++;
        if (_frameTimeAccumulator > 500.0f) {
            stats.frametime = _frameTimeAccumulator / _frameCountAccumulator;
            _frameTimeAccumulator = 0.0f;
            _frameCountAccumulator = 0;
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            _mainCamera.processSDLEvent(e);
            if (e.type == SDL_QUIT)
                done = true;
            
            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    resize_requested = true;
                } 
                else if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    freeze_rendering = true;
                } 
                else if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    freeze_rendering = false;
                }
            }
        }

        _mainCamera.processInput();
        
        if (freeze_rendering) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (resize_requested) resize_swapchain();
        
        run_imgui();
        update_scene(dt / 1000.f);
        draw();
    }
}

void VulkanEngine::draw()
{
    stats.triangle_count = 0;
    stats.drawcall_count = 0;

    // wait last frame
    VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));
    get_current_frame()._deletionQueue.flush();
    get_current_frame()._frameDescriptorAllocator.clear_pools(_device);

    // acquire swapchain image index
    uint32_t swapchainImageIndex;
    VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex);
    if (e == VK_ERROR_OUT_OF_DATE_KHR) {
        resize_requested = true;
        return;
    }

    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));
    VK_CHECK(vkResetCommandBuffer(get_current_frame()._commandBuffer, 0));

    _drawExtent.height = std::min(_swapchainExtent.height, _drawImage.imageExtent.height) * 1.f;
    _drawExtent.width = std::min(_swapchainExtent.width, _drawImage.imageExtent.width) * 1.f;

    VkCommandBuffer cmd = get_current_frame()._commandBuffer;
    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // prepare global scene data
    // UBO and descriptor set are shared by shadow pass and geometry pass
    AllocatedBuffer gpuSceneDataBuffer = create_buffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    get_current_frame()._deletionQueue.push_function([=, this]() {
        destroy_buffer(gpuSceneDataBuffer);
        });
    
    GPUSceneData* sceneUniformData;
    vmaMapMemory(_allocator, gpuSceneDataBuffer.allocation, (void**)&sceneUniformData);

    // camera
    sceneUniformData->view = _mainCamera.getViewMatrix();
    float aspect = (float)_windowExtent.width / (float)_windowExtent.height;
    sceneUniformData->proj = _mainCamera.getProjectionMatrix(aspect);
    sceneUniformData->viewproj = sceneUniformData->proj * sceneUniformData->view;

    // light
    sceneUniformData->ambientColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    sceneUniformData->sunlightColor = _sunlightColor;
    sceneUniformData->sunlightDirection = _sunlightDirection;

    CSMData csm = compute_csmdata();
    for (int i = 0; i < NUM_CASCADES; ++i) {
        sceneUniformData->lightViewproj[i] = csm.lightMatrices[i];
        sceneUniformData->cascadeDistances[i] = csm.planeDistances[i];
    }
    if (_shadowMode < 3) sceneUniformData->lightViewproj[0] = compute_light_matrix();

    sceneUniformData->sunlightColor.w = _enableShadows? 1.f : 0.f;
    sceneUniformData->sunlightDirection.w = _shadowMode;
    // sceneUniformData->activeCascades = NUM_CASCADES;

    vmaUnmapMemory(_allocator, gpuSceneDataBuffer.allocation);

    VkDescriptorSet globalDescriptor = get_current_frame()._frameDescriptorAllocator.allocate(_device, _globalSceneDescriptorLayout);

    DescriptorWriter writer;
    // binding0: UBO
    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    // binding1: shadow map
    writer.write_image(1, _shadowImage.imageView, _shadowSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.update_set(_device, globalDescriptor);

    // shadow pass
    vkutil::transition_image(cmd, _shadowImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    if(_enableShadows) draw_shadow(cmd, globalDescriptor);
    vkutil::transition_image(cmd, _shadowImage.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    // background pass
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    draw_background(cmd);

    // geometry pass
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    draw_geometry(cmd, globalDescriptor);

    // postprocess pass
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    if (_enablePostprocess) draw_postprocess(cmd);

    // copy to swapchain
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkutil::copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

    // draw UI
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    draw_imgui(cmd, _swapchainImageViews[swapchainImageIndex]);

    // from swapchain to present
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    VK_CHECK(vkEndCommandBuffer(cmd));

    // submit
    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame()._swapchainSemaphore);
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame()._renderSemaphore);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));

    // present
    VkPresentInfoKHR presentInfo = vkinit::present_info();
    presentInfo.pSwapchains = &_swapchain;
    presentInfo.swapchainCount = 1;
    presentInfo.pWaitSemaphores = &get_current_frame()._renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pImageIndices = &swapchainImageIndex;

    VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
        resize_requested = true;
    }

    _frameNumber++;
}

FrameData& VulkanEngine::get_current_frame()
{
    return _frames[_frameNumber % FRAME_OVERLAP];
}

void VulkanEngine::run_imgui()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();

    ImGui::NewFrame();

    if (ImGui::Begin("Stats")) {
        ImGui::Text("frametime %.3f ms", stats.frametime);
        ImGui::Text("fps: %.1f", 1000.f / (stats.frametime + 0.0001f));
        ImGui::Text("triangles: %i", stats.triangle_count);
        ImGui::Text("draw call: %i", stats.drawcall_count);
    }
    ImGui::End();

    if (ImGui::Begin("Lighting Debug")) {
        ImGui::Separator();
        ImGui::Checkbox("Enable Shadows", &_enableShadows);
        if (_enableShadows) {
            ImGui::ColorEdit3("Light Color", &_sunlightColor[0]);

            const char* items[] = {"Hard", "PCF", "PCSS", "CSM"};
            ImGui::Combo("Shadow Mode", &_shadowMode, items, IM_ARRAYSIZE(items));
        }
    }
    ImGui::End();

    if (ImGui::Begin("Postprocess")) {
        ImGui::Checkbox("Enable Postprocess", &_enablePostprocess);
    }
    ImGui::End();

    ImGui::Render();
}

void VulkanEngine::draw_background(VkCommandBuffer cmd)
{
    ComputeEffect& effect = backgroundEffects[0];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptorSet, 0, nullptr);
    vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

    vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
}

void VulkanEngine::draw_geometry(VkCommandBuffer cmd, VkDescriptorSet globalDescriptor)
{
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // keep
    VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.clearValue.depthStencil.depth = 1.0f;
    VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);

    vkCmdBeginRendering(cmd, &renderInfo);

    // set viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = (float)_drawExtent.width;
    viewport.height = (float)_drawExtent.height;
    viewport.minDepth = 0;
    viewport.maxDepth = 1;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = _drawExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // bind
    MaterialTemplate* defaultTemplate = _materialSystem.get_template("Opaque");
    if (defaultTemplate) {
        // set0: global data (UBO)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultTemplate->layout, 0, 1, &globalDescriptor, 0, nullptr);
        // set1: bindless texture
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, defaultTemplate->layout, 1, 1, &_bindlessDescriptorSet, 0, nullptr);
    }

    VkPipeline lastPipeline = VK_NULL_HANDLE;
    VkDescriptorSet lastMaterialSet = VK_NULL_HANDLE;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    for (const auto& object : _renderObjects) {
        if (!object.material || !object.material->pipeline) continue;

        if (object.material->pipeline->pipeline != lastPipeline) {
            lastPipeline = object.material->pipeline->pipeline;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lastPipeline);
        }
        // bind material set at 2
        if (object.material->materialSet != lastMaterialSet) {
            lastMaterialSet = object.material->materialSet;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline->layout, 2, 1, &lastMaterialSet, 0, nullptr);
        }
        if (object.mesh->meshBuffer.buffer != lastIndexBuffer) {
            lastIndexBuffer = object.mesh->meshBuffer.buffer;
            vkCmdBindIndexBuffer(cmd, lastIndexBuffer, object.mesh->indexOffset, VK_INDEX_TYPE_UINT32);
        }

        // push constants
        GPUDrawPushConstants pushConstants;
        pushConstants.worldMatrix = object.transform;
        pushConstants.vertexBuffer = object.mesh->meshBuffer.address;
        vkCmdPushConstants(cmd, object.material->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
        
        vkCmdDrawIndexed(cmd, object.indexCount, 1, object.firstIndex, 0, 0);

        stats.drawcall_count++;
        stats.triangle_count += object.indexCount / 3;
    }

    vkCmdEndRendering(cmd);
}

void VulkanEngine::draw_shadow(VkCommandBuffer cmd, VkDescriptorSet globalDescriptor)
{
    int layerCount = _shadowMode < 3 ? 1 : NUM_CASCADES;
    for (int cascadeIndex = 0; cascadeIndex < layerCount; ++cascadeIndex) {
        // VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(_shadowImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(_shadowImageViews[cascadeIndex], VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        depthAttachment.clearValue.depthStencil.depth = 1.0f;
        VkRenderingInfo renderInfo = vkinit::rendering_info(_shadowExtent, nullptr, &depthAttachment);
        renderInfo.colorAttachmentCount = 0; 
        renderInfo.pColorAttachments = nullptr;

        vkCmdBeginRendering(cmd, &renderInfo);

        // set viewport and scissor
        VkViewport viewport = {};
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = (float)_shadowExtent.width;
        viewport.height = (float)_shadowExtent.height;
        viewport.minDepth = 0.f;
        viewport.maxDepth = 1.f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset = { 0, 0 };
        scissor.extent = _shadowExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // bind set and pipeline
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowPipelineLayout, 0, 1, &globalDescriptor, 0, nullptr);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowPipeline);

        // bind index buffer and push constants, then draw
        for (const auto& object : _renderObjects) {
            vkCmdBindIndexBuffer(cmd, object.mesh->meshBuffer.buffer, object.mesh->indexOffset, VK_INDEX_TYPE_UINT32);

            // push constants
            GPUDrawPushConstants pushConstants;
            pushConstants.worldMatrix = object.transform;
            pushConstants.vertexBuffer = object.mesh->meshBuffer.address;
            pushConstants.cascadeIndex = cascadeIndex;
            vkCmdPushConstants(cmd, _shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
        
            vkCmdDrawIndexed(cmd, object.indexCount, 1, object.firstIndex, 0, 0);
        }

        vkCmdEndRendering(cmd);
    }
}

void VulkanEngine::draw_postprocess(VkCommandBuffer cmd)
{
    if (_postprocessPasses.empty()) return;

    vkutil::transition_image(cmd, _postprocessImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    for (int i = 0; i < _postprocessPasses.size(); ++i) {
        VkDescriptorSet curSet = (i % 2 == 0) ? _postprocessDescriptorSets[0] : _postprocessDescriptorSets[1]; 
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _postprocessPasses[i].pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _postprocessPasses[i].layout, 0, 1, &curSet, 0, nullptr);
        vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);

        // barrier
        VkImage curImage = (i % 2 == 0) ? _postprocessImage.image : _drawImage.image;
        vkutil::transition_image(cmd, curImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    // copy to draw image if final result is in postprocess image
    if (_postprocessPasses.size() % 2 != 0) {
        vkutil::transition_image(cmd, _postprocessImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkutil::copy_image_to_image(cmd, _postprocessImage.image, _drawImage.image, _drawExtent, _drawExtent);
        vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }
}

void VulkanEngine::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
    VkRenderingAttachmentInfo colorAttachmentInfo = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_GENERAL);
    VkRenderingInfo renderingInfo = vkinit::rendering_info(_windowExtent, &colorAttachmentInfo, nullptr);

    vkCmdBeginRendering(cmd, &renderingInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
}

void extract_frustum_planes(const glm::mat4& VP, Frustum& frustum) {
    glm::mat4 M = glm::transpose(VP); // glm is col-major
    frustum.planes[0] = M[3] + M[0];
    frustum.planes[1] = M[3] - M[0];
    frustum.planes[2] = M[3] + M[1];
    frustum.planes[3] = M[3] - M[1];
    frustum.planes[4] = M[2];
    frustum.planes[5] = M[3] - M[2];

    for (auto& plane : frustum.planes) {
        float length = glm::length(glm::vec3(plane));
        plane /= length;
    }
}

void VulkanEngine::update_scene(float dt)
{
    _mainCamera.update(dt); 
    _renderObjects.clear();

    std::vector<RenderObject> allObjects;
    if (_sceneRoot) {
        _sceneRoot->refreshTransform(glm::mat4(1.0f), allObjects);
    }

    float aspect = (float)_windowExtent.width / (float)_windowExtent.height;
    glm::mat4 viewProj = _mainCamera.getProjectionMatrix(aspect) * _mainCamera.getViewMatrix();
    Frustum camFrustum;
    extract_frustum_planes(viewProj, camFrustum);
    for (const auto& obj : allObjects) {
        if (is_visible(obj, camFrustum))
            _renderObjects.push_back(obj);
    }

    std::sort(_renderObjects.begin(), _renderObjects.end(), [](const RenderObject& a, const RenderObject& b) {
        if (a.material->passType == MaterialPass::MainColor && b.material->passType != MaterialPass::MainColor) return true;
        if (a.material->passType != MaterialPass::MainColor && b.material->passType == MaterialPass::MainColor) return false;
        return a.material->pipeline < b.material->pipeline;
    });
}

bool VulkanEngine::is_visible(const RenderObject& obj, const Frustum& frustum)
{
    glm::vec3 globalCenter = obj.transform * glm::vec4(obj.mesh->bounds.origin, 1.f);

    float scaleX = glm::length(glm::vec3(obj.transform[0]));
    float scaleY = glm::length(glm::vec3(obj.transform[1]));
    float scaleZ = glm::length(glm::vec3(obj.transform[2]));
    float maxScale = std::max(std::max(scaleX, scaleY), scaleZ);
    float globalRadius = obj.mesh->bounds.sphereRadius * maxScale;

    for (int i = 0; i < 6; i++) {
        // dist = dot(normal, point) + w
        float dist = glm::dot(glm::vec3(frustum.planes[i]), globalCenter) + frustum.planes[i].w;
        
        if (dist < -globalRadius) 
            return false;
    }

    return true;
}

glm::mat4 VulkanEngine::compute_light_matrix() {
    glm::vec3 lightPos = -_sunlightDirection * 60.f;
    glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.f), glm::vec3(0, 1, 0));
    // glm::mat4 lightProj = glm::perspective(glm::radians(_mainCamera.fov), 1.f, _mainCamera.zNear, _mainCamera.zFar);
    glm::mat4 lightProj = glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, _mainCamera.zNear, _mainCamera.zFar);

    lightProj[1][1] *= -1; 
    
    return lightProj * lightView;
}

std::vector<glm::vec4> getFrustumCornerWorld(const glm::mat4& proj, const glm::mat4& view) {
    const auto inv = glm::inverse(proj * view);
    std::vector<glm::vec4> frustumCorner;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k) {
                const glm::vec4 pt = inv * glm::vec4(2.f * i - 1.f, 2.f * j - 1.f, 2.f * k - 1.f, 1.f);
                frustumCorner.push_back(pt / pt.w);
            }
    
    return frustumCorner;
}

glm::mat4 VulkanEngine::getLightMatrix(const float zNear, const float zFar) {
    float aspect = (float)_windowExtent.width / (float)_windowExtent.height;
    glm::mat4 proj = glm::perspective(glm::radians(_mainCamera.fov), aspect, zNear, zFar);
    const auto corners = getFrustumCornerWorld(proj, _mainCamera.getViewMatrix());

    glm::vec3 center = glm::vec3(0.f);
    for (const auto& v : corners)
        center += glm::vec3(v);
    center /= corners.size();

    glm::vec3 lightDir = glm::normalize(-_sunlightDirection);
    const auto lightView = glm::lookAt(center + lightDir, center, glm::vec3(0, 1, 0));

    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();
    for (const auto& v : corners) {
        const auto trf = lightView * v;
        minX = std::min(minX, trf.x);
        maxX = std::max(maxX, trf.x);
        minY = std::min(minY, trf.y);
        maxY = std::max(maxY, trf.y);
        minZ = std::min(minZ, trf.z);
        maxZ = std::max(maxZ, trf.z);
    }

    float zMult = 10.f;
    if (minZ < 0) minZ *= zMult;
    else minZ /= zMult;
    if (maxZ < 0) maxZ /= zMult;
    else maxZ *= zMult;
    
    const glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
    return lightProj * lightView;
}

// Practical Split Scheme from GPU Gems3 Chapter 10
float compute_split(float n, float f, int i) {
    float p = (float)i / (float)NUM_CASCADES;
    float C_log = n * pow(f / n, p);
    float C_uni = n + (f - n) * p;
    float lambda = 0.5f;
    return lambda * C_log + (1.f - lambda) * C_uni;
}

CSMData VulkanEngine::compute_csmdata() {
    CSMData csm;
    // std::vector<float> shadowCascadeLevels{ _mainCamera.zFar / 50.0f, _mainCamera.zFar / 25.0f, _mainCamera.zFar / 10.0f, _mainCamera.zFar };
    float shadowCascadeLevels[NUM_CASCADES];
    for (int i = 0; i < NUM_CASCADES; ++i) {
        shadowCascadeLevels[i] = compute_split(_mainCamera.zNear, _mainCamera.zFar, i + 1);
        csm.planeDistances[i] = shadowCascadeLevels[i];
        float curNear = i == 0 ? _mainCamera.zNear : shadowCascadeLevels[i - 1];
        csm.lightMatrices[i] = getLightMatrix(curNear, shadowCascadeLevels[i]);
    }

    return csm;
}