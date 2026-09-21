#pragma once

#include <memory>
#include <string>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan_raii.hpp>

class Texture;
class Buffer;

class Material
{
  public:
    std::string name = "Default";
    glm::vec4 baseColorFactor{1.0f};
    float metallic = 1.0f;
    float roughness = 1.0f;
    float ao = 1.0f;
    glm::vec3 emissive{0.0f};
    float normalScale = 1.0f;

    std::string albedoMap;
    std::string normalMap;
    std::string metallicMap;
    std::string roughnessMap;
    std::string aoMap;
    std::string emissiveMap;

    std::string alphaMode = "OPAQUE";
    float alphaCutoff = 0.5f;
    bool doubleSided = false;

    [[nodiscard]] bool hasAlbedoMap() const;
    void createDescriptorSet(
        const vk::raii::PhysicalDevice &physicalDevice,
        const vk::raii::Device &device,
        vk::DescriptorPool descriptorPool,
        vk::DescriptorSetLayout descriptorSetLayout,
        std::shared_ptr<Texture> texture);
    void bind(
        vk::raii::CommandBuffer &commandBuffer,
        vk::PipelineLayout pipelineLayout) const;

  private:
    std::shared_ptr<Texture> albedoTexture;
    std::shared_ptr<Buffer> materialBuffer;
    std::shared_ptr<vk::raii::DescriptorSet> descriptorSet;
};
