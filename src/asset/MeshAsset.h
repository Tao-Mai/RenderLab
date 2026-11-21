#pragma once

#include "core/MetadataAnnotations.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <variant>
#include <vector>
#include "asset/Asset.h"
#include "core/math/Bounds.h"
struct MaterialAsset;

struct RL_RECORD MeshAsset : Asset
{
    using ID                              = AssetID<MeshAsset>;
    static constexpr std::string_view dir = "Mesh";

    struct RL_RECORD Submesh
    {
        RL_FIELD("firstIndex") uint32_t               firstIndex = 0;
        RL_FIELD("indexCount") uint32_t               indexCount = 0;
        RL_FIELD("materialId") AssetID<MaterialAsset> materialId;
    };

    struct RL_TYPE("Sphere") Sphere
    {
        RL_FIELD("radius") double   radius   = 1.0;
        RL_FIELD("zMin") double   zMin     = -1.0;
        RL_FIELD("zMax") double   zMax     = 1.0;
        RL_FIELD("phiMax") double   phiMax   = 360.0;
        RL_FIELD("segments") uint32_t segments = 128;
        RL_FIELD("rings") uint32_t rings    = 64;
    };

    struct RL_TYPE("Cylinder") Cylinder
    {
        RL_FIELD("radius") double   radius   = 1.0;
        RL_FIELD("zMin") double   zMin     = -1.0;
        RL_FIELD("zMax") double   zMax     = 1.0;
        RL_FIELD("phiMax") double   phiMax   = 360.0;
        RL_FIELD("segments") uint32_t segments = 128;
    };

    struct RL_TYPE("Disk") Disk
    {
        RL_FIELD("height") double   height      = 0.0;
        RL_FIELD("radius") double   radius      = 1.0;
        RL_FIELD("innerRadius") double   innerRadius = 0.0;
        RL_FIELD("phiMax") double   phiMax      = 360.0;
        RL_FIELD("segments") uint32_t segments    = 128;
    };

    using Primitive = std::variant<Sphere, Cylinder, Disk>;

    RL_FIELD("id") ID                                   id;
    RL_FIELD("source") Source                               source;
    RL_FIELD("path") std::optional<std::filesystem::path> path;
    // Null imports the whole file; a value identifies mesh-local geometry within that file.
    RL_FIELD("meshIndex") std::optional<uint32_t>              meshIndex;
    RL_FIELD("importScale") float                                importScale = 1.0f;
    RL_FIELD("geometry") std::filesystem::path                geometry;
    RL_FIELD("primitive") std::optional<Primitive>             primitive;
    RL_FIELD("bounds") Aabb                                 bounds;
    RL_FIELD("submeshes") std::vector<Submesh>                 submeshes;
};

#include RL_METADATA_HEADER("metadata/src/asset/MeshAsset.generated.h")
