#include "asset/AssetManager.h"
#include "asset/AssetTypes.h"
#include "asset/BuiltinAssets.h"
#include "asset/pbrt/PbrtMaterial.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Serializer.h"
#include "editor/EditorCamera.h"
#include "scene/SceneManager.h"
#include "scene/component/LightTypes.h"
#include "scene/component/RenderComponent.h"
#include "scene/component/TransformComponent.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>

#include <gtest/gtest.h>

static_assert(AssetType<TextureAsset>);
static_assert(AssetType<MeshAsset>);
static_assert(AssetType<MaterialAsset>);
static_assert(AssetType<ShaderAsset>);
static_assert(AssetType<SceneAsset>);
static_assert(AssetType<EnvironmentMapAsset>);
static_assert(AssetType<SamplerAsset>);
static_assert(!AssetType<Asset>);
static_assert(std::same_as<MaterialOverride, GltfMaterial>);
static_assert(!std::is_convertible_v<TextureAsset::ID, MeshAsset::ID>);

TEST(AssetIDTest, AssetsExposePersistentFieldsDirectly)
{
    AssetTypes::forEach(
        []<AssetType T>
        {
            T asset{};
            asset.id          = typename T::ID{"direct-fields"};
            const json stored = Serialize(asset);
            EXPECT_EQ(stored.at("id"), (json{{"value", "direct-fields"}}));

            T restored{};
            Deserialize(stored, restored);
            EXPECT_EQ(restored.id, asset.id);
            EXPECT_EQ(Serialize(restored), stored);
        }
    );
}

TEST(AssetIDTest, SerializesAsReflectedObjectAndKeepsAssetType)
{
    const TextureAsset::ID textureId{"test-texture"};
    const json             stored = {{"value", "test-texture"}};
    EXPECT_EQ(Serialize(textureId), stored);

    TextureAsset::ID parsed;
    Deserialize(stored, parsed);
    EXPECT_EQ(parsed, textureId);

    std::unordered_map<TextureAsset::ID, int> textures;
    textures.emplace(textureId, 7);
    EXPECT_EQ(textures.at(TextureAsset::ID{"test-texture"}), 7);
}

TEST(AssetIDTest, ResolvesBuiltinLutsWithoutAnAssetDirectory)
{
    AssetManager assets;
    const auto& brdf = assets.get(BuiltinAssets::Texture::kBrdfLut);
    const auto& sheen = assets.get(BuiltinAssets::Texture::kSheenLut);
    EXPECT_EQ(brdf.source, Source::Builtin);
    EXPECT_EQ(brdf.format, ImageFormat::RG8);
    EXPECT_EQ(sheen.source, Source::Builtin);
    EXPECT_EQ(sheen.format, ImageFormat::RGBA32F);
    for (const auto* texture : {&brdf, &sheen})
    {
        EXPECT_EQ(texture->colorSpace, ColorSpace::Linear);
        EXPECT_EQ(texture->layout, ImageLayout::Image2D);
        EXPECT_EQ(texture->mipLevels, 1u);
        EXPECT_FALSE(texture->path);
        EXPECT_FALSE(texture->binary);
    }
}

TEST(AssetIDTest, RoundTripsNestedMeshSubmesh)
{
    MeshAsset mesh;
    mesh.id          = MeshAsset::ID{"sphere"};
    mesh.source      = Source::Builtin;
    mesh.geometry    = "binary/sphere.geometry";
    mesh.importScale = 2.0f;
    mesh.submeshes.push_back({
        .firstIndex = 3,
        .indexCount = 6,
        .materialId = MaterialAsset::ID{"white"},
    });

    const json serialized = Serialize(mesh);
    EXPECT_EQ(serialized.at("id").at("value"), "sphere");
    EXPECT_EQ(serialized.at("importScale"), 2.0f);
    EXPECT_TRUE(serialized.at("meshIndex").is_null());
    EXPECT_TRUE(serialized.at("path").is_null());
    EXPECT_EQ(serialized.at("submeshes")[0].at("materialId").at("value"), "white");

    MeshAsset parsed;
    Deserialize(json::parse(serialized.dump()), parsed);
    ASSERT_EQ(parsed.submeshes.size(), 1);
    EXPECT_EQ(parsed.submeshes[0].firstIndex, 3);
    EXPECT_EQ(parsed.submeshes[0].indexCount, 6);
    EXPECT_EQ(parsed.submeshes[0].materialId, MaterialAsset::ID{"white"});
    EXPECT_EQ(Serialize(parsed), serialized);
}

TEST(AssetIDTest, RoundTripsSamplerAndTextureBinding)
{
    SamplerAsset sampler;
    sampler.id            = SamplerAsset::ID{"custom"};
    sampler.magFilter     = SamplerAsset::Filter::Nearest;
    sampler.addressModeU  = SamplerAsset::AddressMode::ClampToBorder;
    sampler.compareEnable = true;
    sampler.compareOp     = SamplerAsset::CompareOp::LessOrEqual;
    sampler.borderColor   = SamplerAsset::BorderColor::FloatOpaqueWhite;
    sampler.maxLod        = 0.0f;

    const json   samplerJson = Serialize(sampler);
    SamplerAsset parsedSampler;
    Deserialize(samplerJson, parsedSampler);
    EXPECT_EQ(parsedSampler.id, sampler.id);
    EXPECT_EQ(parsedSampler.magFilter, sampler.magFilter);
    EXPECT_EQ(parsedSampler.addressModeU, sampler.addressModeU);
    EXPECT_EQ(parsedSampler.compareOp, sampler.compareOp);
    EXPECT_EQ(parsedSampler.borderColor, sampler.borderColor);
    EXPECT_EQ(parsedSampler.maxLod, 0.0f);
    EXPECT_EQ(Serialize(parsedSampler), samplerJson);

    GltfMaterial material;
    material.id               = MaterialAsset::ID{"white"};
    material.baseColorTexture = TextureBinding{TextureAsset::ID{"white"}, SamplerAsset::ID{"linearRepeat"}};
    const json materialJson   = Serialize(material);
    EXPECT_TRUE(materialJson.at("roughness").is_null());
    EXPECT_EQ(materialJson.at("baseColorTexture").at("textureID").at("value"), "white");
    GltfMaterial parsedMaterial;
    Deserialize(materialJson, parsedMaterial);
    ASSERT_TRUE(parsedMaterial.baseColorTexture);
    EXPECT_EQ(parsedMaterial.baseColorTexture->textureID, TextureAsset::ID{"white"});
    EXPECT_EQ(parsedMaterial.baseColorTexture->samplerID, SamplerAsset::ID{"linearRepeat"});
    EXPECT_EQ(Serialize(parsedMaterial), materialJson);
}

TEST(AssetIDTest, RejectsMissingAndUnknownAssetFields)
{
    GltfMaterial material;
    material.id               = MaterialAsset::ID{"strict-material"};
    material.baseColorTexture = TextureBinding{TextureAsset::ID{"white"}, SamplerAsset::ID{"linearRepeat"}};
    const json    stored      = Serialize(material);
    GltfMaterial parsed;

    json invalid = stored;
    invalid.erase("roughness");
    EXPECT_DEATH(Deserialize(invalid, parsed), "");
    invalid = stored;
    invalid["id"].erase("value");
    EXPECT_DEATH(Deserialize(invalid, parsed), "");

    invalid               = stored;
    invalid["unexpected"] = true;
    EXPECT_DEATH(Deserialize(invalid, parsed), "");
    invalid                                   = stored;
    invalid["baseColorTexture"]["unexpected"] = true;
    EXPECT_DEATH(Deserialize(invalid, parsed), "");
    invalid                     = stored;
    invalid["id"]["unexpected"] = true;
    EXPECT_DEATH(Deserialize(invalid, parsed), "");
}

TEST(AssetIDTest, MaterialEqualityDetectsParameterAndBindingChanges)
{
    GltfMaterial material;
    material.id               = MaterialAsset::ID{"white"};
    material.roughness        = 0.5f;
    material.baseColorTexture = TextureBinding{TextureAsset::ID{"white"}, SamplerAsset::ID{"linearRepeat"}};

    GltfMaterial copy;
    Deserialize(Serialize(material), copy);
    EXPECT_EQ(copy, material);
    copy.roughness.reset();
    EXPECT_NE(copy, material);
    copy                             = material;
    copy.baseColorTexture->textureID = TextureAsset::ID{"albedo"};
    EXPECT_NE(copy, material);
    copy                             = material;
    copy.baseColorTexture->samplerID = SamplerAsset::ID{"linearClamp"};
    EXPECT_NE(copy, material);
}

TEST(AssetIDTest, JsonIoSavesAndReplacesAssetsInTheUnifiedFormat)
{
    struct TemporaryAsset
    {
        std::filesystem::path directory = std::filesystem::path{RENDERLAB_SOURCE_DIR} / "test" /
            ("json_io_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::path file = directory / MaterialAsset::dir / "persisted.json";

        ~TemporaryAsset()
        {
            std::error_code error;
            std::filesystem::remove(file, error);
            std::filesystem::remove(file.string() + ".tmp", error);
            std::filesystem::remove(file.parent_path(), error);
            std::filesystem::remove(directory, error);
        }
    } temporary;

    AssetManager assets;
    assets.init(temporary.directory);
    GltfMaterial material;
    material.id              = MaterialAsset::ID{"persisted"};
    material.baseColorFactor = glm::vec4{0.2f, 0.4f, 0.6f, 1.0f};
    material.normalTexture   = TextureBinding{TextureAsset::ID{"normal"}, SamplerAsset::ID{"linearRepeat"}};
    assets.save(material);
    auto restored = asset_json::load<std::unique_ptr<MaterialAsset>>(temporary.file);
    EXPECT_TRUE(Serialize(restored).contains("GltfMaterial"));
    EXPECT_EQ(restored->as<GltfMaterial>(), material);

    material.roughness   = 0.3f;
    material.doubleSided = false;
    assets.save(material);
    restored = asset_json::load<std::unique_ptr<MaterialAsset>>(temporary.file);
    EXPECT_EQ(restored->as<GltfMaterial>(), material);
    EXPECT_FALSE(std::filesystem::exists(temporary.file.string() + ".tmp"));

    std::ifstream input{temporary.file};
    EXPECT_EQ(json::parse(input), Serialize(restored));
    input.close();

    PbrtMaterial offline;
    offline.id          = material.id;
    offline.entity.type = "dielectric";
    offline.entity.parameters.push_back({.type = "float", .name = "eta", .value = PbrtFloatValues{{1.6}}});
    assets.save(offline);
    EXPECT_EQ(assets.get(offline.id).as<PbrtMaterial>(), offline);
    EXPECT_EQ(assets.loadedIds<MaterialAsset>().size(), 1u);
    restored = asset_json::load<std::unique_ptr<MaterialAsset>>(temporary.file);
    EXPECT_EQ(restored->as<PbrtMaterial>(), offline);
    EXPECT_EQ(Serialize(restored).at("PbrtMaterial").at("entity").at("type"), "dielectric");

    assets.shutdown();
    AssetManager reloaded;
    reloaded.init(temporary.directory);
    EXPECT_EQ(reloaded.get(offline.id).as<PbrtMaterial>(), offline);
    reloaded.shutdown();
}

TEST(AssetIDTest, PbrtMaterialPreservesTypedParameterAlternatives)
{
    PbrtMaterial material;
    material.id          = MaterialAsset::ID{"offline"};
    material.entity.type = "diffuse";
    material.entity.parameters.push_back(
        {.type = "rgb", .name = "reflectance", .colorSpace = "srgb", .value = PbrtFloatValues{{0.2, 0.3, 0.4}}}
    );
    auto stored = Serialize(material);
    EXPECT_EQ(stored.at("entity").at("type"), "diffuse");
    PbrtMaterial restored;
    Deserialize(stored, restored);
    EXPECT_EQ(restored, material);

    material.entity.type       = "conductor";
    material.entity.parameters = {{.type = "spectrum", .name = "eta", .value = PbrtStringValues{{"metal-Cu-eta"}}}};
    stored                     = Serialize(material);
    EXPECT_EQ(stored.at("entity").at("type"), "conductor");
    Deserialize(stored, restored);
    EXPECT_EQ(restored, material);
    EXPECT_DEATH(Deserialize(json{{"UnknownValues", json::object()}}, restored.entity.parameters.front().value), "");
}

TEST(AssetIDTest, AllPersistedAssetsRoundTrip)
{
    SceneManager registration;
    registration.init();
    registration.shutdown();
    const auto root  = std::filesystem::path{RENDERLAB_SOURCE_DIR} / "assets";
    AssetManager assets;
    assets.init(root);
    size_t     count = 0;
    AssetTypes::forEach(
        [&]<AssetType T>
        {
            const auto directory = root / T::dir;
            if (!std::filesystem::is_directory(directory))
                return;

            for (const auto& entry : std::filesystem::directory_iterator(directory))
            {
                if (!entry.is_regular_file() || entry.path().extension() != ".json")
                    continue;
                SCOPED_TRACE(entry.path().string());
                using Stored = std::conditional_t<std::is_polymorphic_v<T>, std::unique_ptr<T>, T>;
                const auto    asset = asset_json::load<Stored>(entry.path());
                std::ifstream input{entry.path()};
                EXPECT_EQ(Serialize(asset), json::parse(input));
                ++count;
            }
        }
    );
    EXPECT_GT(count, 0);
    assets.shutdown();
}

TEST(AssetIDTest, BuiltinSamplersKeepExpectedFiltersAndAddressModes)
{
    struct ConfiguredAssets
    {
        ConfigManager config;
        AssetManager  assets;
        SceneManager  scenes;

        ConfiguredAssets()
        {
            context().config = &config;
            config.init();
            scenes.init();
            assets.init(config.paths().assets);
        }

        ~ConfiguredAssets()
        {
            assets.shutdown();
            scenes.shutdown();
            config.shutdown();
            context().config = nullptr;
        }
    } configured;

    const auto& linearRepeat  = configured.assets.get<SamplerAsset>(BuiltinAssets::Sampler::linearRepeat);
    const auto& linearClamp   = configured.assets.get<SamplerAsset>(BuiltinAssets::Sampler::linearClamp);
    const auto& nearestRepeat = configured.assets.get<SamplerAsset>(BuiltinAssets::Sampler::nearestRepeat);
    const auto& nearestClamp  = configured.assets.get<SamplerAsset>(BuiltinAssets::Sampler::nearestClamp);

    const AssetManager& assets  = configured.assets;
    const auto&         texture = assets.get(BuiltinAssets::Texture::white);
    static_assert(std::same_as<decltype(assets.get(TextureAsset::ID{})), const TextureAsset&>);
    EXPECT_EQ(assets.find(texture.id), &texture);
    EXPECT_EQ(assets.find(BuiltinAssets::Sampler::linearClamp), &linearClamp);
    EXPECT_EQ(assets.find(TextureAsset::ID{"missing-test-texture"}), nullptr);

    EXPECT_EQ(linearRepeat.magFilter, SamplerAsset::Filter::Linear);
    EXPECT_EQ(linearRepeat.addressModeU, SamplerAsset::AddressMode::Repeat);
    EXPECT_EQ(linearClamp.magFilter, SamplerAsset::Filter::Linear);
    EXPECT_EQ(linearClamp.addressModeU, SamplerAsset::AddressMode::ClampToEdge);
    EXPECT_EQ(nearestRepeat.magFilter, SamplerAsset::Filter::Nearest);
    EXPECT_EQ(nearestRepeat.addressModeU, SamplerAsset::AddressMode::Repeat);
    EXPECT_EQ(nearestClamp.magFilter, SamplerAsset::Filter::Nearest);
    EXPECT_EQ(nearestClamp.addressModeU, SamplerAsset::AddressMode::ClampToEdge);
    EXPECT_FALSE(linearClamp.maxLod.has_value());
}

TEST(AssetIDTest, LoadsEnvironmentTextureBindings)
{
    const auto file =
        std::filesystem::path{RENDERLAB_SOURCE_DIR} / "assets" / EnvironmentMapAsset::dir / "grasslands_sunset_4k.json";
    ASSERT_TRUE(std::filesystem::is_regular_file(file));

    const auto environment = asset_json::load<EnvironmentMapAsset>(file);
    EXPECT_EQ(environment.radiance.textureID, TextureAsset::ID{"grasslands_sunset_4k"});
    EXPECT_EQ(environment.irradiance.samplerID, SamplerAsset::ID{"linearClamp"});
    EXPECT_EQ(environment.prefilteredSpecular.samplerID, SamplerAsset::ID{"linearClamp"});
}

TEST(AssetIDTest, LoadsCornellBoxWithIndependentEditorCamera)
{
    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    const auto file  = std::filesystem::path{RENDERLAB_SOURCE_DIR} / "assets" / SceneAsset::dir / "cornell_box.json";
    const auto scene = asset_json::load<SceneAsset>(file);
    manager.load(scene);

    EXPECT_EQ(manager.scene().id, SceneAsset::ID{"cornell_box"});
    EXPECT_EQ(manager.scene().actors.size(), 8u);
    EXPECT_EQ(managerCamera.worldPosition(), glm::vec3(0.0f, 2.75f, 10.5f));
    AssetManager assets;
    for (const auto& actor : manager.scene().actors)
        if (const auto* render = actor->getComponent<RenderComponent>())
            EXPECT_NE(assets.find<MeshAsset>(render->meshId), nullptr);

    const auto* light = manager.scene().actors.back()->getComponent<RectAreaLightComponent>();
    EXPECT_TRUE(light && light->enabled && light->castShadow);
    const auto direction = manager.scene().actors.back()->transform().rotation * glm::vec3(0.0f, 0.0f, -1.0f);
    EXPECT_NEAR(direction.y, -1.0f, 1.0e-6f);
    EXPECT_EQ(Serialize(manager.scene()), Serialize(scene));
    manager.shutdown();
}

TEST(AssetIDTest, RoundTripsTypedIdsInsideSceneComponents)
{
    SceneManager registration;
    registration.init();
    registration.shutdown();
    const auto file = std::filesystem::path{RENDERLAB_SOURCE_DIR} / "assets" / SceneAsset::dir / "default.json";
    ASSERT_TRUE(std::filesystem::is_regular_file(file));

    const SceneAsset scene = asset_json::load<SceneAsset>(file);
    ASSERT_EQ(scene.id, SceneAsset::ID{"default"});

    std::ifstream input{file};
    EXPECT_EQ(Serialize(scene), json::parse(input));

    bool foundRender = false;
    for (const auto& actor : scene.actors)
    {
        if (const auto* render = actor->getComponent<RenderComponent>())
        {
            EXPECT_FALSE(render->meshId.empty());
            foundRender = true;
        }
    }
    EXPECT_TRUE(foundRender);

    SceneAsset parsed;
    Deserialize(Serialize(scene), parsed);
    EXPECT_EQ(parsed.id, scene.id);
    EXPECT_EQ(parsed.environment.up, scene.environment.up);
}
