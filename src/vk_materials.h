#pragma once

#include "vk_types.h"
#include "vk_pipelines.h"

class VulkanEngine;

enum class MaterialPass : uint8_t {
    MainColor,
    Transparent,
    Other
};

struct MaterialConstants {
    glm::vec4 colorFactors;
    glm::vec4 metal_rough_factors;
};

struct MaterialTemplate {
    VkPipeline pipeline;
    VkPipelineLayout layout;
    MaterialPass passType;
    VkDescriptorSetLayout descriptorLayout;
};

struct MaterialInstance {
    MaterialTemplate* pipeline;
    VkDescriptorSet materialSet;
    MaterialPass passType;
    MaterialConstants params;
    AllocatedBuffer paramsBuffer; // uniform buffer
};

class MaterialSystem {
public:
    void init(VulkanEngine* engine);
    void cleanup();

    MaterialTemplate* register_template(const std::string& name, PipelineBuilder& builder, VkDescriptorSetLayout layout, MaterialPass passType);
    MaterialTemplate* get_template(const std::string& name);
    MaterialInstance* build_instance(MaterialTemplate* materialTemplate, MaterialConstants params, AllocatedImage* albedo, AllocatedImage* normal, AllocatedImage* metalRough);

private:
    VulkanEngine* _engine;
    std::unordered_map<std::string, MaterialTemplate> _templateCache;
};