#pragma once
class ConfigManager;
class AssetManager;
class VulkanContext;
struct SceneAsset;
bool drawRendererSettings(ConfigManager& config, AssetManager& assets, const VulkanContext& vulkan, SceneAsset& scene);
