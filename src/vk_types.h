#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <deque>
#include <functional>
#include <fmt/core.h>
#include <memory>
#include <array>

#include <vk_descriptors.h>
#include <glm/gtx/transform.hpp>

#define NUM_CASCADES 4

struct AllocatedImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct AllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
    VkDeviceAddress address;
};

struct DeletionQueue {
    std::deque<std::function<void()>> deletors;

    void push_function(std::function<void()>&& function) {
        deletors.push_back(function);
    }

    void flush() {
        // reverse iterate the deletion queue
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
            (*it)(); // execute lambda function
        }
        deletors.clear();
    }
};

struct ComputePushConstants {
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

struct ComputeEffect {
    const char* name;

    VkPipeline pipeline;
    VkPipelineLayout layout;

    ComputePushConstants data;
}; 

struct GPUDrawPushConstants {
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBuffer;
    int cascadeIndex;
};

struct EngineStats {
    float frametime;
    int triangle_count;
    int drawcall_count;
    float mesh_draw_time;
};

struct Vertex {
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 color;
};

struct GPUSceneData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewproj;
    glm::mat4 lightViewproj[NUM_CASCADES];
    float cascadeDistances[NUM_CASCADES];
    glm::vec4 ambientColor;
    glm::vec4 sunlightDirection;
    glm::vec4 sunlightColor;
};

struct MaterialInstance;

struct GeoSurface {
    uint32_t startIndex;
    uint32_t count;
    MaterialInstance* material = nullptr;
};

struct Bounds {
    glm::vec3 origin;
    float sphereRadius;
    glm::vec3 extents;
};

struct Frustum {
    std::array<glm::vec4, 6> planes;
};

struct MeshAsset {
    std::string name;
    AllocatedBuffer meshBuffer;
    std::vector<GeoSurface> surfaces; // multiple surface materials per mesh
    size_t indexOffset;
    Bounds bounds;
};

struct RenderObject {
    MeshAsset* mesh;
    MaterialInstance* material;
    glm::mat4 transform;

    uint32_t indexCount;
    uint32_t firstIndex;
};

struct Node : std::enable_shared_from_this<Node> {
    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;
    
    glm::mat4 localTransform = glm::mat4(1.f);
    glm::mat4 worldTransform = glm::mat4(1.f);

    MeshAsset* mesh = nullptr;

    void refreshTransform(const glm::mat4& parentMatrix, std::vector<RenderObject>& outDrawList) {
        worldTransform = parentMatrix * localTransform;
        if (mesh) {
            for (auto& surface : mesh->surfaces) {
                RenderObject obj;
                obj.mesh = mesh;
                obj.transform = worldTransform;
                obj.material = surface.material;
                obj.indexCount = surface.count;
                obj.firstIndex = surface.startIndex;
                outDrawList.push_back(obj);
            }   
        }
        for (auto& c : children) 
            c->refreshTransform(worldTransform, outDrawList);
    }

    void addChild(std::shared_ptr<Node> child) {
        child->parent = weak_from_this();
        children.push_back(child);
    }
};

#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
             fmt::print("Detected Vulkan error: {}", string_VkResult(err)); \
            abort();                                                    \
        }                                                               \
    } while (0)