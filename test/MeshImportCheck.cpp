// Standalone local check; does not import textures/materials or change scene JSON.
#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Serializer.h"
#include "editor/MeshImportPreview.h"

#include <cmath>
#include <imgui.h>
#include <iostream>

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(argc == 2 || argc == 3);
    const auto root     = std::filesystem::path{argv[1]};
    const auto source   = root / "resource/car/scene.gltf";
    auto       prepared = AssetImporter::prepareMesh(source);
    CHECK(prepared.geometry.vertices.size() >= 179652);
    CHECK(prepared.geometry.indices.size() == 679218);
    CHECK(prepared.submeshes.size() == 533);
    const glm::vec3 size = prepared.boundsMax - prepared.boundsMin;
    CHECK(size.z > 0.05f && size.z < 0.052f);

    MeshImportPreview preview;
    preview.load(source);
    ImGui::CreateContext();
    auto& io       = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1000.0f, 800.0f};
    io.DeltaTime   = 1.0f / 60.0f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    CHECK(io.Fonts->Build());
    for (int frame = 0; frame < 2; ++frame)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({900.0f, 700.0f});
        ImGui::Begin("Import preview check");
        preview.draw(50.0f);
        ImGui::End();
        ImGui::Render();
    }
    CHECK(ImGui::GetDrawData()->TotalVtxCount > 0);
    ImGui::DestroyContext();
    preview.clear();

    if (argc == 3)
    {
        CHECK(std::string_view{argv[2]} == "--bake-existing");
        ConfigManager config;
        context().config = &config;
        config.init();
        AssetDataManager data;
        context().assetDataManager = &data;
        data.init(config.paths().assets);

        MeshAsset asset = asset_json::load<MeshAsset>(root / "assets/Mesh/scene.json");
        CHECK(asset.importScale == 50.0f);
        const auto originalIndices = prepared.geometry.indices;
        for (auto& vertex : prepared.geometry.vertices)
            vertex.position *= asset.importScale;
        const auto output = data.writeGeometry(asset.id, prepared.geometry);
        CHECK(output == asset.geometry);
        const auto restored = data.readGeometry(asset);
        CHECK(restored.indices == originalIndices);
        CHECK(restored.vertices.size() == prepared.geometry.vertices.size());
        for (size_t index = 0; index < restored.vertices.size(); ++index)
        {
            const auto& actual   = restored.vertices[index];
            const auto& expected = prepared.geometry.vertices[index];
            CHECK(
                actual.position == expected.position && actual.normal == expected.normal &&
                actual.texcoord == expected.texcoord && actual.tangent == expected.tangent
            );
        }
        data.shutdown();
        config.shutdown();
        context() = {};
    }
    std::cout << "Mesh preparation, preview and geometry checks passed. Scaled XYZ: " << size.x * 50.0f << ", "
              << size.y * 50.0f << ", " << size.z * 50.0f << '\n';
}
