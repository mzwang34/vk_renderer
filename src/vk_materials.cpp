#pragma once

#include "vk_materials.h"
#include "vk_engine.h"
#include "vk_initializers.h"

void MaterialSystem::init(VulkanEngine* engine)
{
    _engine = engine;
}

void MaterialSystem::cleanup()
{
    for (auto& [name, temp] : _templateCache) {
        vkDestroyPipeline(_engine->_device, temp.pipeline, nullptr);
        vkDestroyPipelineLayout(_engine->_device, temp.layout, nullptr);
    }
}

MaterialTemplate* MaterialSystem::get_template(const std::string& name)
{
    auto it = _templateCache.find(name);
    if (it != _templateCache.end()) 
        return &it->second;
    return nullptr;
}

MaterialTemplate* MaterialSystem::register_template(const std::string& name, PipelineBuilder& builder, VkDescriptorSetLayout layout, MaterialPass passType)
{
    auto& newTemplate = _templateCache[name];
    newTemplate.passType = passType;
    newTemplate.descriptorLayout = layout;

    std::vector<VkDescriptorSetLayout> setLayouts = {
        _engine->_globalSceneDescriptorLayout, // set 0: global ubo
        _engine->_bindlessTextureLayout, // set 1: bindless texture
        layout // set 2: material ubo
    };

    VkPipelineLayoutCreateInfo layoutInfo = vkinit::pipeline_layout_create_info();
    layoutInfo.setLayoutCount = (uint32_t)setLayouts.size();
    layoutInfo.pSetLayouts = setLayouts.data();

    VkPushConstantRange pushConstant {};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(GPUDrawPushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    layoutInfo.pPushConstantRanges = &pushConstant;
    layoutInfo.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(_engine->_device, &layoutInfo, nullptr, &newTemplate.layout));

    builder._pipelineLayout = newTemplate.layout;
    newTemplate.pipeline = builder.build_pipeline(_engine->_device);

    return &newTemplate;
}

MaterialInstance* MaterialSystem::build_instance(MaterialTemplate* materialTemplate, MaterialConstants params, AllocatedImage* albedo, AllocatedImage* normal, AllocatedImage* metalRough)
{
    MaterialInstance* mat = new MaterialInstance();
    mat->pipeline = materialTemplate;
    mat->params = params;
    mat->passType = materialTemplate->passType;
    mat->paramsBuffer = _engine->create_buffer(sizeof(MaterialConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    _engine->_mainDeletionQueue.push_function([=]() {
        _engine->destroy_buffer(mat->paramsBuffer);
        });

    // upload params to UBO
    void* data;
    vmaMapMemory(_engine->_allocator, mat->paramsBuffer.allocation, &data);
    memcpy(data, &mat->params, sizeof(MaterialConstants));
    vmaUnmapMemory(_engine->_allocator, mat->paramsBuffer.allocation);

    mat->materialSet = _engine->_globalDescriptorAllocator.allocate(_engine->_device, materialTemplate->descriptorLayout);

    DescriptorWriter writer;
    // binding 0: material constants (UBO)
    writer.write_buffer(0, mat->paramsBuffer.buffer, sizeof(MaterialConstants), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

    writer.update_set(_engine->_device, mat->materialSet);
    return mat;
}