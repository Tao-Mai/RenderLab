#include "asset/AssetDesc.h"
#include "asset/JsonIo.h"
#include "ecs/Light.h"
#include "ecs/Render.h"
#include "ecs/Transform.h"

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
            const ecs::Render* render = entry->second.try_cast<ecs::Render>();
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
