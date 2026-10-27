#pragma once

#include "asset/Asset.h"
#include "asset/pbrt/PbrtTypes.h"

struct PbrtSceneAsset : Asset
{
    using ID                              = AssetID<PbrtSceneAsset>;
    static constexpr std::string_view dir = "PbrtScene";

    ID                                     id;
    std::string                            readerRevision;
    std::filesystem::path                  source;
    std::string                            renderingSpace;
    glm::dmat4                             renderFromWorld{1.0};
    PbrtEntity                             camera;
    PbrtTransform                          worldFromCamera;
    std::optional<uint32_t>                cameraMedium;
    PbrtEntity                             film;
    PbrtEntity                             sampler;
    PbrtEntity                             integrator;
    PbrtEntity                             pixelFilter;
    PbrtEntity                             accelerator;
    std::vector<PbrtOption>                options;
    std::vector<AssetID<MaterialAsset>>    materials;
    std::vector<AssetID<PbrtTextureAsset>> textures;
    std::vector<PbrtShape>                 shapes;
    std::vector<uint32_t>                  worldShapes;
    std::vector<PbrtEntity>                areaLights;
    std::vector<PbrtLight>                 lights;
    std::vector<PbrtMedium>                media;
    std::vector<PbrtObjectDefinition>      objects;
    std::vector<PbrtInstance>              instances;
    std::vector<PbrtFileReference>         files;

    bool operator==(const PbrtSceneAsset&) const = default;
};
