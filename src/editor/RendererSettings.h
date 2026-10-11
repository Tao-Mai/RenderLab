#pragma once
class ConfigManager;
class AssetManager;
class VulkanContext;
void drawRendererSettings(ConfigManager& config, AssetManager& assets, const VulkanContext& vulkan, float cameraNear);
