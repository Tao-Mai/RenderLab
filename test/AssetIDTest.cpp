#include "asset/Asset.h"
#include "asset/AssetDescManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/JsonIo.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "ecs/component/LightComponent.h"
#include "ecs/component/RenderComponent.h"
#include "ecs/component/TransformComponent.h"

#include <filesystem>
#include <string>
#include <type_traits>
#include <unordered_map>

#include <gtest/gtest.h>
#include <rfl/json.hpp>

static_assert(AssetType<Texture>);
static_assert(AssetType<Mesh>);
static_assert(!std::is_convertible_v<Texture::ID, Mesh::ID>);

TEST(AssetIDTest, SerializesAsStringAndKeepsAssetType)
{
    const Texture::ID textureId{"lut_ggx"};
    EXPECT_EQ(rfl::json::write(textureId), "\"lut_ggx\"");

    const auto parsed = rfl::json::read<Texture::ID>("\"lut_ggx\"");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(*parsed, textureId);

    std::unordered_map<Texture::ID, int> textures;
    textures.emplace(textureId, 7);
    EXPECT_EQ(textures.at(Texture::ID{"lut_ggx"}), 7);
}

TEST(AssetIDTest, LoadsDescriptorFromRegisteredDirectory)
{
    const auto file = std::filesystem::path{RENDERLAB_SOURCE_DIR} /
        "assets" / Texture::dir / "lut_ggx.json";
    ASSERT_TRUE(std::filesystem::is_regular_file(file));

    const Texture::Desc desc = asset_json::load<Texture::Desc>(file);
    EXPECT_EQ(desc.id, Texture::ID{"lut_ggx"});
    EXPECT_EQ(desc.format, ImageFormat::RG8);
}

TEST(AssetIDTest, LoadsRendererBrdfLutBinding)
{
    const auto file = std::filesystem::path{RENDERLAB_SOURCE_DIR} /
        "config" / "config.json";
    const AppConfig config = asset_json::load<AppConfig>(file);

    EXPECT_EQ(config.renderer.brdfLut.textureID, Texture::ID{"lut_ggx"});
    EXPECT_EQ(config.renderer.brdfLut.samplerID, BuiltinAssets::Sampler::linearClamp);
}

TEST(AssetIDTest, RoundTripsNestedMeshSubmesh)
{
    Mesh::Desc mesh;
    mesh.id = Mesh::ID{"sphere"};
    mesh.source = Source::Builtin;
    mesh.geometry = "binary/sphere.geometry";
    mesh.submeshes.push_back({
        .firstIndex = 3,
        .indexCount = 6,
        .materialId = Material::ID{"white"},
    });

    const std::string serialized =
        rfl::json::write<rfl::SnakeCaseToPascalCase>(mesh);
    EXPECT_NE(serialized.find("\"Submeshes\""), std::string::npos);
    EXPECT_NE(serialized.find("\"MaterialId\""), std::string::npos);

    const auto parsed = rfl::json::read<Mesh::Desc,
                                        rfl::SnakeCaseToPascalCase, rfl::NoExtraFields>(serialized);
    ASSERT_TRUE(parsed);
    ASSERT_EQ(parsed->submeshes.size(), 1);
    EXPECT_EQ(parsed->submeshes[0].firstIndex, 3);
    EXPECT_EQ(parsed->submeshes[0].indexCount, 6);
    EXPECT_EQ(parsed->submeshes[0].materialId, Material::ID{"white"});
}

TEST(AssetIDTest, RoundTripsSamplerAndTextureBinding)
{
    Sampler::Desc sampler;
    sampler.id = Sampler::ID{"custom"};
    sampler.magFilter = Sampler::Desc::Filter::Nearest;
    sampler.addressModeU = Sampler::Desc::AddressMode::ClampToBorder;
    sampler.compareEnable = true;
    sampler.compareOp = Sampler::Desc::CompareOp::LessOrEqual;
    sampler.borderColor = Sampler::Desc::BorderColor::FloatOpaqueWhite;
    sampler.maxLod = 0.0f;

    const std::string samplerJson =
        rfl::json::write<rfl::SnakeCaseToPascalCase>(sampler);
    const auto parsedSampler = rfl::json::read<Sampler::Desc,
        rfl::SnakeCaseToPascalCase, rfl::NoExtraFields>(samplerJson);
    ASSERT_TRUE(parsedSampler);
    EXPECT_EQ(parsedSampler->id, sampler.id);
    EXPECT_EQ(parsedSampler->magFilter, sampler.magFilter);
    EXPECT_EQ(parsedSampler->addressModeU, sampler.addressModeU);
    EXPECT_EQ(parsedSampler->compareOp, sampler.compareOp);
    EXPECT_EQ(parsedSampler->borderColor, sampler.borderColor);
    EXPECT_EQ(parsedSampler->maxLod, 0.0f);

    Material::Desc material;
    material.id = Material::ID{"white"};
    material.baseColorTexture = TextureBinding{
        Texture::ID{"white"}, Sampler::ID{"linearRepeat"}};
    const std::string materialJson =
        rfl::json::write<rfl::SnakeCaseToPascalCase>(material);
    const auto parsedMaterial = rfl::json::read<Material::Desc,
        rfl::SnakeCaseToPascalCase, rfl::NoExtraFields>(materialJson);
    ASSERT_TRUE(parsedMaterial);
    ASSERT_TRUE(parsedMaterial->baseColorTexture);
    EXPECT_EQ(parsedMaterial->baseColorTexture->textureID, Texture::ID{"white"});
    EXPECT_EQ(parsedMaterial->baseColorTexture->samplerID, Sampler::ID{"linearRepeat"});
}

TEST(AssetIDTest, BuiltinSamplersKeepExpectedFiltersAndAddressModes)
{
    struct ConfiguredDescriptors
    {
        ConfigManager config;
        AssetDescManager descriptors;

        ConfiguredDescriptors()
        {
            context().config = &config;
            config.init();
            descriptors.init();
        }

        ~ConfiguredDescriptors()
        {
            descriptors.shutdown();
            config.shutdown();
            context().config = nullptr;
        }
    } configured;

    const auto& linearRepeat = configured.descriptors.desc<Sampler>(
        BuiltinAssets::Sampler::linearRepeat);
    const auto& linearClamp = configured.descriptors.desc<Sampler>(
        BuiltinAssets::Sampler::linearClamp);
    const auto& nearestRepeat = configured.descriptors.desc<Sampler>(
        BuiltinAssets::Sampler::nearestRepeat);
    const auto& nearestClamp = configured.descriptors.desc<Sampler>(
        BuiltinAssets::Sampler::nearestClamp);

    EXPECT_EQ(linearRepeat.magFilter, Sampler::Desc::Filter::Linear);
    EXPECT_EQ(linearRepeat.addressModeU, Sampler::Desc::AddressMode::Repeat);
    EXPECT_EQ(linearClamp.magFilter, Sampler::Desc::Filter::Linear);
    EXPECT_EQ(linearClamp.addressModeU, Sampler::Desc::AddressMode::ClampToEdge);
    EXPECT_EQ(nearestRepeat.magFilter, Sampler::Desc::Filter::Nearest);
    EXPECT_EQ(nearestRepeat.addressModeU, Sampler::Desc::AddressMode::Repeat);
    EXPECT_EQ(nearestClamp.magFilter, Sampler::Desc::Filter::Nearest);
    EXPECT_EQ(nearestClamp.addressModeU, Sampler::Desc::AddressMode::ClampToEdge);
    EXPECT_FALSE(linearClamp.maxLod.has_value());
}

TEST(AssetIDTest, LoadsEnvironmentTextureBindings)
{
    const auto file = std::filesystem::path{RENDERLAB_SOURCE_DIR} /
        "assets" / EnvironmentMap::dir / "grasslands_sunset_4k.json";
    ASSERT_TRUE(std::filesystem::is_regular_file(file));

    const auto environment = asset_json::load<EnvironmentMap::Desc>(file);
    EXPECT_EQ(environment.radiance.textureID, Texture::ID{"grasslands_sunset_4k"});
    EXPECT_EQ(environment.irradiance.samplerID, Sampler::ID{"linearClamp"});
    EXPECT_EQ(environment.prefilteredSpecular.samplerID,
              Sampler::ID{"linearClamp"});
}

TEST(AssetIDTest, RoundTripsTypedIdsInsideSceneComponents)
{
    const auto file = std::filesystem::path{RENDERLAB_SOURCE_DIR} /
        "assets" / Scene::dir / "default.json";
    ASSERT_TRUE(std::filesystem::is_regular_file(file));

    const Scene::Desc scene = asset_json::load<Scene::Desc>(file);
    ASSERT_EQ(scene.id, Scene::ID{"default"});

    bool foundRender = false;
    for (const Scene::Desc::Object& object : scene.objects)
    {
        if (const auto entry = object.components.find("Render");
            entry != object.components.end())
        {
            const ecs::RenderComponent* render = entry->second.try_cast<ecs::RenderComponent>();
            ASSERT_NE(render, nullptr);
            EXPECT_FALSE(render->meshId.empty());
            foundRender = true;
        }
    }
    EXPECT_TRUE(foundRender);

    const std::string serialized =
        rfl::json::write<rfl::SnakeCaseToPascalCase>(scene);
    const auto parsed = rfl::json::read<Scene::Desc,
                                        rfl::SnakeCaseToPascalCase, rfl::NoExtraFields>(serialized);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->id, scene.id);
    EXPECT_EQ(parsed->environment.environmentMap,
              scene.environment.environmentMap);
}
