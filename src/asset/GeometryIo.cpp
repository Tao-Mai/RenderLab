#include "asset/GeometryIo.h"

#include "core/Logger.h"

#include <array>
#include <cstdint>
#include <fstream>

namespace
{
constexpr uint32_t geometryMagic = 0x4853454du;
constexpr uint32_t geometryVersion = 1;
}

namespace geometry_io
{
void write(const std::filesystem::path& file, const MeshGeometry& mesh)
{
    CHECK(!mesh.vertices.empty() && !mesh.indices.empty(),
        "imported mesh has no geometry: {}", file.string());
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    CHECK(output, "cannot write mesh geometry: {}", file.string());
    const std::array<uint32_t, 4> header{
        geometryMagic,
        geometryVersion,
        static_cast<uint32_t>(mesh.vertices.size()),
        static_cast<uint32_t>(mesh.indices.size()),
    };
    output.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
    for (const Vertex& vertex : mesh.vertices)
    {
        const std::array<float, 8> values{
            vertex.position.x, vertex.position.y, vertex.position.z,
            vertex.normal.x, vertex.normal.y, vertex.normal.z,
            vertex.texcoord.x, vertex.texcoord.y,
        };
        output.write(reinterpret_cast<const char*>(values.data()), sizeof(values));
    }
    output.write(
        reinterpret_cast<const char*>(mesh.indices.data()),
        static_cast<std::streamsize>(mesh.indices.size() * sizeof(uint32_t)));
    CHECK(output, "failed writing mesh geometry: {}", file.string());
}

MeshGeometry read(const std::filesystem::path& file)
{
    std::ifstream input(file, std::ios::binary);
    CHECK(input, "cannot open mesh geometry: {}", file.string());
    std::array<uint32_t, 4> header{};
    input.read(reinterpret_cast<char*>(header.data()), sizeof(header));
    CHECK(input && header[0] == geometryMagic && header[1] == geometryVersion,
        "invalid mesh geometry header: {}", file.string());
    const uint64_t expectedSize = sizeof(header) +
        static_cast<uint64_t>(header[2]) * 8 * sizeof(float) +
        static_cast<uint64_t>(header[3]) * sizeof(uint32_t);
    CHECK(std::filesystem::file_size(file) == expectedSize && header[2] != 0 && header[3] != 0,
        "invalid mesh geometry size: {}", file.string());
    MeshGeometry geometry;
    geometry.vertices.reserve(header[2]);
    geometry.indices.resize(header[3]);
    for (uint32_t index = 0; index < header[2]; ++index)
    {
        std::array<float, 8> values{};
        input.read(reinterpret_cast<char*>(values.data()), sizeof(values));
        geometry.vertices.push_back({
            .position = {values[0], values[1], values[2]},
            .normal = {values[3], values[4], values[5]},
            .texcoord = {values[6], values[7]},
        });
    }
    input.read(
        reinterpret_cast<char*>(geometry.indices.data()),
        static_cast<std::streamsize>(geometry.indices.size() * sizeof(uint32_t)));
    CHECK(input, "failed reading mesh geometry: {}", file.string());
    return geometry;
}
}
