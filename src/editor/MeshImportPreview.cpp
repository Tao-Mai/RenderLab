#include "editor/MeshImportPreview.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

void MeshImportPreview::load(const std::filesystem::path& source)
{
    mesh  = AssetImporter::prepareMesh(source);
    orbit = {0.7f, 0.3f};
    zoom  = 1.0f;
}

void MeshImportPreview::clear() { mesh.reset(); }

void MeshImportPreview::draw(float importScale)
{
    if (!mesh)
        return;

    const glm::vec3 originalSize = mesh->boundsMax - mesh->boundsMin;
    const bool      validScale   = std::isfinite(importScale) && importScale > 0.0f;
    const float     scale        = validScale ? importScale : 1.0f;
    const glm::vec3 scaledSize   = originalSize * scale;
    ImGui::Text("Original XYZ (m): %.6g x %.6g x %.6g", originalSize.x, originalSize.y, originalSize.z);
    ImGui::Text("Imported XYZ (m): %.6g x %.6g x %.6g", scaledSize.x, scaledSize.y, scaledSize.z);
    ImGui::TextDisabled("Wireframe preview | drag to orbit, wheel to zoom | yellow cube = 1 m");

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{560.0f, 320.0f};
    ImGui::InvisibleButton("Mesh preview", size);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const auto delta = ImGui::GetIO().MouseDelta;
        orbit.x -= delta.x * 0.01f;
        orbit.y = std::clamp(orbit.y + delta.y * 0.01f, -1.4f, 1.4f);
    }
    if (ImGui::IsItemHovered())
        zoom = std::clamp(zoom * std::exp(-ImGui::GetIO().MouseWheel * 0.15f), 0.2f, 8.0f);

    const glm::vec3 modelMin = mesh->boundsMin * scale;
    const glm::vec3 modelMax = mesh->boundsMax * scale;
    const glm::vec3 cubeMin{modelMax.x + 0.3f, modelMin.y, (modelMin.z + modelMax.z) * 0.5f - 0.5f};
    const glm::vec3 cubeMax  = cubeMin + glm::vec3{1.0f};
    const glm::vec3 lower    = glm::min(modelMin, cubeMin);
    const glm::vec3 upper    = glm::max(modelMax, cubeMax);
    const glm::vec3 center   = (lower + upper) * 0.5f;
    const float     radius   = std::max(glm::length(upper - lower) * 0.5f, 0.1f);
    const float     distance = radius * 3.2f * zoom;
    const glm::vec3 direction{
        std::cos(orbit.y) * std::sin(orbit.x),
        std::sin(orbit.y),
        std::cos(orbit.y) * std::cos(orbit.x)
    };
    const glm::mat4 viewProjection =
        glm::perspective(glm::radians(45.0f), size.x / size.y, radius * 0.001f, distance + radius * 4.0f) *
        glm::lookAt(center + direction * distance, center, glm::vec3{0.0f, 1.0f, 0.0f});

    auto*        draw = ImGui::GetWindowDrawList();
    const ImVec2 bottomRight{origin.x + size.x, origin.y + size.y};
    draw->AddRectFilled(origin, bottomRight, IM_COL32(25, 28, 33, 255));
    draw->PushClipRect(origin, bottomRight, true);
    const auto line = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color)
    {
        const auto project = [&](const glm::vec3& p, ImVec2& screen)
        {
            const glm::vec4 clip = viewProjection * glm::vec4{p, 1.0f};
            if (clip.w <= 0.0f || clip.z < 0.0f || clip.z > clip.w)
                return false;
            const glm::vec2 ndc = glm::vec2{clip} / clip.w;
            screen = {origin.x + (ndc.x + 1.0f) * size.x * 0.5f, origin.y + (1.0f - ndc.y) * size.y * 0.5f};
            return true;
        };
        ImVec2 first, second;
        if (project(a, first) && project(b, second))
            draw->AddLine(first, second, color);
    };

    // Bound preview drawing cost without changing the imported geometry.
    constexpr size_t kMaxPreviewTriangles = 4000;
    const auto&      geometry             = mesh->geometry;
    const size_t     triangleCount        = geometry.indices.size() / 3;
    const size_t     stride = std::max<size_t>((triangleCount + kMaxPreviewTriangles - 1) / kMaxPreviewTriangles, 1);
    for (size_t triangle = 0; triangle < triangleCount; triangle += stride)
    {
        const auto      a          = geometry.vertices[geometry.indices[triangle * 3]].position * scale;
        const auto      b          = geometry.vertices[geometry.indices[triangle * 3 + 1]].position * scale;
        const auto      c          = geometry.vertices[geometry.indices[triangle * 3 + 2]].position * scale;
        constexpr ImU32 kMeshColor = IM_COL32(150, 195, 220, 150);
        line(a, b, kMeshColor);
        line(b, c, kMeshColor);
        line(c, a, kMeshColor);
    }

    std::array<glm::vec3, 8> corners;
    for (size_t index = 0; index < corners.size(); ++index)
        corners[index] =
            {index & 1 ? cubeMax.x : cubeMin.x, index & 2 ? cubeMax.y : cubeMin.y, index & 4 ? cubeMax.z : cubeMin.z};
    for (size_t index = 0; index < corners.size(); ++index)
        for (size_t bit = 1; bit <= 4; bit <<= 1)
            if (!(index & bit))
                line(corners[index], corners[index | bit], IM_COL32(255, 210, 75, 255));
    draw->PopClipRect();
}
