#include <vk_engine.h>
#include <vk_materials.h>
#include <vk_initializers.h>
#include <vk_images.h>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/util.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <ktx.h>
#include <ktxvulkan.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

VkFilter extract_filter(fastgltf::Filter filter) {
    switch (filter) {
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::NearestMipMapLinear:
        return VK_FILTER_NEAREST;
    case fastgltf::Filter::Linear:
    case fastgltf::Filter::LinearMipMapNearest:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_FILTER_LINEAR;
    }
}

VkSamplerAddressMode extract_address_mode(fastgltf::Wrap wrap) {
    switch (wrap) {
    case fastgltf::Wrap::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case fastgltf::Wrap::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case fastgltf::Wrap::Repeat:
    default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter)
{
    switch (filter) {
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::LinearMipMapNearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case fastgltf::Filter::NearestMipMapLinear:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
    VK_CHECK(vkResetFences(_device, 1, &_immFence));
    VK_CHECK(vkResetCommandPool(_device, _immCommandPool, 0));

    VkCommandBuffer cmd = _immCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

    VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    VkBufferCreateInfo bufferInfo {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo vmaallocInfo {};
    vmaallocInfo.usage = memoryUsage;
    vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT; 

    AllocatedBuffer newBuffer;

    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo,
        &newBuffer.buffer,
        &newBuffer.allocation,
        &newBuffer.info));
    
    VkBufferDeviceAddressInfo deviceAdressInfo {};
    deviceAdressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    deviceAdressInfo.buffer = newBuffer.buffer;

    newBuffer.address = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

    return newBuffer;
}

void VulkanEngine::destroy_buffer(const AllocatedBuffer& buffer)
{
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

std::shared_ptr<AllocatedImage> VulkanEngine::create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    size_t data_size = size.depth * size.width * size.height * 4;
     
    // write data to CPU-visible VkBuffer, then upload to VkImage through GPU instruction vkCmdCopyBufferToImage
    AllocatedBuffer uploadbuffer = create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    memcpy(uploadbuffer.info.pMappedData, data, data_size);

    // create GPU image
    VkImageUsageFlags finalUsage = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImageCreateInfo imgInfo = vkinit::image_create_info(format, size, finalUsage);
    if (mipmapped) {
        imgInfo.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
    }

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    auto newImage = std::make_shared<AllocatedImage>();
    newImage->imageFormat = format;
    newImage->imageExtent = size;

    vmaCreateImage(_allocator, &imgInfo, &allocInfo, &newImage->image, &newImage->allocation, nullptr);

    // copy image
    immediate_submit([&](VkCommandBuffer cmd) {
        vkutil::transition_image(cmd, newImage->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copyRegion = {};
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = size;
        vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, newImage->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        if (mipmapped) {
            vkutil::generate_mipmaps(cmd, newImage->image, VkExtent2D{size.width, size.height});
        } else {
            vkutil::transition_image(cmd, newImage->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    });

    // create image view
    VkImageViewCreateInfo viewInfo = vkinit::imageview_create_info(newImage->image, format, VK_IMAGE_ASPECT_COLOR_BIT);
    if (mipmapped) {
        viewInfo.subresourceRange.levelCount = imgInfo.mipLevels;
    }
    vkCreateImageView(_device, &viewInfo, nullptr, &newImage->imageView);

    destroy_buffer(uploadbuffer);
    _mainDeletionQueue.push_function([=]() {
        vkDestroyImageView(_device, newImage->imageView, nullptr);
        vmaDestroyImage(_allocator, newImage->image, newImage->allocation);
    });

    return newImage;
}

void VulkanEngine::update_bindless_texture(int index, VkImageView view, VkSampler sampler)
{
    VkDescriptorImageInfo imageInfo {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = view;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet write {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = _bindlessDescriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(_device, 1, &write, 0, nullptr);
}

AllocatedBuffer VulkanEngine::upload_mesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);
    const size_t totalSize = vertexBufferSize + indexBufferSize;

    AllocatedBuffer newBuffer = create_buffer(totalSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | 
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | 
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    AllocatedBuffer staging = create_buffer(totalSize, 
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
        VMA_MEMORY_USAGE_CPU_TO_GPU);

    void* data = staging.info.pMappedData;

    memcpy(data, vertices.data(), vertexBufferSize);

    memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

    immediate_submit([&](VkCommandBuffer cmd) {
        VkBufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = totalSize;

        vkCmdCopyBuffer(cmd, staging.buffer, newBuffer.buffer, 1, &copyRegion);
    });

    destroy_buffer(staging);

    _mainDeletionQueue.push_function([=]() {
        destroy_buffer(newBuffer);
    });

    return newBuffer;
}

std::shared_ptr<Node> VulkanEngine::load_gltf(std::string name, std::string fileName)
{
    fastgltf::Parser parser{};
    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;
    
    fastgltf::GltfDataBuffer data;
    data.loadFromFile(fileName);

    std::filesystem::path path = fileName;
    auto type = fastgltf::determineGltfFileType(&data);
    fastgltf::Expected<fastgltf::Asset> asset(fastgltf::Error::None);
    if (type == fastgltf::GltfType::glTF) {
        asset = parser.loadGLTF(&data, path.parent_path(), gltfOptions);
    } else if (type == fastgltf::GltfType::GLB) {
        asset = parser.loadBinaryGLTF(&data, path.parent_path(), gltfOptions);
    } else  {
        fmt::println("filed to determine glTF type");
        return nullptr;
    }
    if (asset.error() != fastgltf::Error::None) {
        fmt::println("filed to parse glTF");
        return nullptr;
    }
    fastgltf::Asset& gltf = asset.get();

    // load samplers
    std::vector<VkSampler> samplers;
    for (fastgltf::Sampler& sampler : gltf.samplers) {
        VkSamplerCreateInfo samplerInfo {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
        samplerInfo.minLod = 0;
        samplerInfo.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
        samplerInfo.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));
        samplerInfo.mipmapMode = extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));
        samplerInfo.addressModeU = extract_address_mode(sampler.wrapS);
        samplerInfo.addressModeV = extract_address_mode(sampler.wrapT);
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        VkSampler newSampler;
        vkCreateSampler(_device, &samplerInfo, nullptr, &newSampler);
        samplers.push_back(newSampler);
        _mainDeletionQueue.push_function([=](){ vkDestroySampler(_device, newSampler, nullptr); });
    }

    // load textures
    std::vector<std::shared_ptr<AllocatedImage>> images;
    std::vector<int> textureIndices;
    for (fastgltf::Image& image : gltf.images) {
        std::shared_ptr<AllocatedImage> newImg;

        auto load_from_memory = [&](const unsigned char* buf, size_t len) {
            int w, h, nrChannels;
            unsigned char* pixels = stbi_load_from_memory(buf, static_cast<int>(len), &w, &h, &nrChannels, 4);
            if (pixels) {
                VkExtent3D extent = { (uint32_t)w, (uint32_t)h, 1 };
                newImg = create_image(pixels, extent, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_SAMPLED_BIT, true);
                stbi_image_free(pixels);
            }
        };

        std::visit(fastgltf::visitor {
            [](auto& arg) {},
            [&](fastgltf::sources::URI& filePath) {
                int w, h, nrChannels;
                std::string fullPath = (std::filesystem::path(fileName).parent_path() / filePath.uri.path()).string();
                unsigned char* pixels = stbi_load(fullPath.c_str(), &w, &h, &nrChannels, 4);
                if (pixels) {
                    VkExtent3D extent = { (uint32_t)w, (uint32_t)h, 1 };
                    newImg = create_image(pixels, extent, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_SAMPLED_BIT, true);
                    stbi_image_free(pixels);
                }
            },
            [&](fastgltf::sources::Vector& vector) {
                load_from_memory(reinterpret_cast<const unsigned char*>(vector.bytes.data()), vector.bytes.size());
            },
            [&](fastgltf::sources::BufferView& view) {
                auto& bufferView = gltf.bufferViews[view.bufferViewIndex];
                auto& buffer = gltf.buffers[bufferView.bufferIndex];
                std::visit(fastgltf::visitor {
                    [&](fastgltf::sources::ByteView& byteView) {
                        load_from_memory(reinterpret_cast<const unsigned char*>(byteView.bytes.data() + bufferView.byteOffset), bufferView.byteLength);
                    },
                    [&](fastgltf::sources::Vector& vec) {
                        load_from_memory(reinterpret_cast<const unsigned char*>(vec.bytes.data() + bufferView.byteOffset), bufferView.byteLength);
                    },
                    [](auto&) {}
                }, buffer.data);
            },
        }, image.data);

        if (newImg) {
            int id = _globalTextureIndex++;
            update_bindless_texture(id, newImg->imageView, _defaultSamplerLinear);
            images.push_back(newImg);
            textureIndices.push_back(id);
        } else {
            int id = _globalTextureIndex++;
            update_bindless_texture(id, _errorCheckerboardImage->imageView, _defaultSamplerLinear);
            images.push_back(_errorCheckerboardImage);
            textureIndices.push_back(id);
        }
    }

    // load materials
    std::vector<MaterialInstance*> materials;
    for (fastgltf::Material& mat : gltf.materials) {
        MaterialInstance* newMat = new MaterialInstance();
        
        newMat->params.colorFactors = glm::vec4(mat.pbrData.baseColorFactor[0], mat.pbrData.baseColorFactor[1], mat.pbrData.baseColorFactor[2], mat.pbrData.baseColorFactor[3]);
        newMat->params.metal_rough_factors = glm::vec4(mat.pbrData.metallicFactor, mat.pbrData.roughnessFactor, 0, 0);

        std::string templateName = mat.alphaMode == fastgltf::AlphaMode::Blend ? "Transparent" : "Opaque";
        MaterialTemplate* matTemplate = _materialSystem.get_template(templateName);
        if (!matTemplate) matTemplate = _materialSystem.get_template("Opaque");

        newMat->params.albedoID = 0; 
        newMat->params.normalID = 1;
        newMat->params.metalRoughID = 0;

        AllocatedImage* albedo = _whiteTexture.get();
        if (mat.pbrData.baseColorTexture.has_value()) {
            size_t imgIdx = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
            newMat->params.albedoID = textureIndices[imgIdx];
        }

        AllocatedImage* metalRough = _whiteTexture.get();
        if (mat.pbrData.metallicRoughnessTexture.has_value()) {
            size_t imgIdx = gltf.textures[mat.pbrData.metallicRoughnessTexture.value().textureIndex].imageIndex.value();
            newMat->params.normalID = textureIndices[imgIdx];
        }

        AllocatedImage* normal = _defaultNormalTexture.get();
        if (mat.normalTexture.has_value()) {
            size_t imgIdx = gltf.textures[mat.normalTexture.value().textureIndex].imageIndex.value();
            newMat->params.metalRoughID = textureIndices[imgIdx];
        }
        MaterialInstance* createdMat = _materialSystem.build_instance(matTemplate, newMat->params, albedo, normal, metalRough);
        materials.push_back(createdMat);
        delete newMat;
    }
    
    if (materials.empty()) {
        MaterialInstance* defaultMat = _materialSystem.build_instance(
            _materialSystem.get_template("Opaque"), 
            {}, 
            _whiteTexture.get(), _defaultNormalTexture.get(), _whiteTexture.get());
        materials.push_back(defaultMat);
    }

    // load meshes
    std::vector<std::shared_ptr<MeshAsset>> meshes;
    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;

    for (size_t i = 0; i < gltf.meshes.size(); i++) {
        fastgltf::Mesh& mesh = gltf.meshes[i];
        auto newMesh = std::make_shared<MeshAsset>();
        std::string uniqueName = name + "_" + (mesh.name.empty() ? std::to_string(i) : std::string(mesh.name));
        newMesh->name = uniqueName;

        indices.clear();
        vertices.clear();

        glm::vec3 minPos = glm::vec3(100000.f);
        glm::vec3 maxPos = glm::vec3(-100000.f);

        for (auto&& p : mesh.primitives) {
            GeoSurface newSurface;
            newSurface.startIndex = (uint32_t)indices.size();
            
            if (p.indicesAccessor.has_value()) {
                newSurface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;
            } else {
                continue;
            }

            size_t initial_vtx = vertices.size();
            {
                fastgltf::Accessor& indexAccessor = gltf.accessors[p.indicesAccessor.value()];
                indices.reserve(indices.size() + indexAccessor.count);
                fastgltf::iterateAccessor<std::uint32_t>(gltf, indexAccessor, [&](std::uint32_t idx) {
                    indices.push_back(idx + initial_vtx);
                });
            }

            {
                fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
                vertices.resize(vertices.size() + posAccessor.count);
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor, [&](glm::vec3 v, size_t index) {
                    Vertex newvtx;
                    newvtx.position = v;
                    newvtx.normal = { 1, 0, 0 };
                    newvtx.color = glm::vec4(1.f);
                    newvtx.uv_x = 0; newvtx.uv_y = 0;
                    vertices[initial_vtx + index] = newvtx;
                    minPos = glm::min(minPos, v);
                    maxPos = glm::max(maxPos, v);
                });
            }

            auto normals = p.findAttribute("NORMAL");
            if (normals != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[normals->second], [&](glm::vec3 v, size_t index) {
                    vertices[initial_vtx + index].normal = v;
                });
            }

            auto uv = p.findAttribute("TEXCOORD_0");
            if (uv != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[uv->second], [&](glm::vec2 v, size_t index) {
                    vertices[initial_vtx + index].uv_x = v.x;
                    vertices[initial_vtx + index].uv_y = v.y;
                });
            }

            auto colors = p.findAttribute("COLOR_0");
            if (colors != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[colors->second], [&](glm::vec4 v, size_t index) {
                    vertices[initial_vtx + index].color = v;
                });
            }

            if (p.materialIndex.has_value()) {
                newSurface.material = materials[p.materialIndex.value()];
            } else {
                newSurface.material = materials[0];
            }

            newMesh->surfaces.push_back(newSurface);
        }
        newMesh->bounds.origin = (minPos + maxPos) * 0.5f;
        newMesh->bounds.extents = (maxPos - minPos) * 0.5f;
        newMesh->bounds.sphereRadius = glm::length(newMesh->bounds.extents);
        const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
        newMesh->meshBuffer = upload_mesh(indices, vertices);
        newMesh->indexOffset = vertexBufferSize;
        meshes.push_back(newMesh);
        _meshAssets[uniqueName] = newMesh;
    }

    // buil node
    std::vector<std::shared_ptr<Node>> nodes;
    
    for (fastgltf::Node& node : gltf.nodes) {
        std::shared_ptr<Node> newNode = std::make_shared<Node>();

        std::visit(fastgltf::visitor {
            [&](fastgltf::Node::TransformMatrix& matrix) {
                newNode->localTransform = glm::make_mat4(matrix.data());
            },
            [&](fastgltf::Node::TRS& transform) {
                glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
                glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
                glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

                glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
                glm::mat4 rm = glm::toMat4(rot);
                glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

                newNode->localTransform = tm * rm * sm;
            },
            [](auto&){} 
        }, node.transform);

        if (node.meshIndex.has_value()) {
            newNode->mesh = meshes[node.meshIndex.value()].get();
        }
        
        nodes.push_back(newNode);
    }

    for (size_t i = 0; i < gltf.nodes.size(); i++) {
        fastgltf::Node& gltfNode = gltf.nodes[i];
        std::shared_ptr<Node>& sceneNode = nodes[i];

        for (auto& childIdx : gltfNode.children) {
            sceneNode->addChild(nodes[childIdx]);
        }
    }

    auto topNode = std::make_shared<Node>();
    topNode->localTransform = glm::mat4(1.f);

    for (auto& node : nodes) {
        if (node->parent.expired()) {
            topNode->addChild(node);
        }
    }

    return topNode;
}

// https://github.com/KhronosGroup/KTX-Software/blob/main/examples/vkload.cpp
void VulkanEngine::load_cubemap(const char* filename, AllocatedImage& outImage) {
    ktxTexture* kTexture;
    KTX_error_code result = ktxTexture_CreateFromNamedFile(filename, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);
    if (result != KTX_SUCCESS) {
        fmt::print("failed to load ktx texture image!");
        return;
    }

    ktxVulkanDeviceInfo kvdi;
    ktxVulkanDeviceInfo_Construct(&kvdi, _physical_device, _device, _graphicsQueue, _immCommandPool, nullptr);

    ktxVulkanTexture vkTexture;
    result = ktxTexture_VkUploadEx(kTexture, &kvdi, &vkTexture, 
                                   VK_IMAGE_TILING_OPTIMAL,
                                   VK_IMAGE_USAGE_SAMPLED_BIT,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (result != KTX_SUCCESS) {
        fmt::print("failed to upload ktx texture!");
        ktxTexture_Destroy(kTexture);
        return;
    }

    // create image view (ktxTexture_VkUploadEx has created image)
    outImage.image = vkTexture.image;
    outImage.memory = vkTexture.deviceMemory;
    outImage.imageFormat = vkTexture.imageFormat;
    outImage.imageExtent = { vkTexture.width, vkTexture.height, 1 };
    outImage.allocation = nullptr; // signal not allocated by vma, avoid vma destroy image crash

    VkImageViewCreateInfo viewInfo = vkinit::imageview_create_info(outImage.image, outImage.imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.subresourceRange.layerCount = 6;
    viewInfo.subresourceRange.levelCount = vkTexture.levelCount;
    vkCreateImageView(_device, &viewInfo, nullptr, &outImage.imageView);

    ktxTexture_Destroy(kTexture);
}