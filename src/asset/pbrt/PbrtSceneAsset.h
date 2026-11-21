#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/Asset.h"
#include "asset/pbrt/PbrtTypes.h"

struct SceneAsset;

struct RL_RECORD PbrtSceneAsset : Asset
{
    using ID                              = AssetID<PbrtSceneAsset>;
    static constexpr std::string_view dir = "PbrtScene";

    RL_FIELD("id") ID                                     id;
    RL_FIELD("scene") AssetID<SceneAsset>                    scene;
    RL_FIELD("readerRevision") std::string                            readerRevision;
    RL_FIELD("source") std::filesystem::path                  source;
    RL_FIELD("renderingSpace") std::string                            renderingSpace;
    RL_FIELD("renderFromWorld") glm::dmat4                             renderFromWorld{1.0};
    RL_FIELD("camera") PbrtEntity                             camera;
    RL_FIELD("worldFromCamera") PbrtTransform                          worldFromCamera;
    RL_FIELD("cameraMedium") std::optional<uint32_t>                cameraMedium;
    RL_FIELD("film") PbrtEntity                             film;
    RL_FIELD("sampler") PbrtEntity                             sampler;
    RL_FIELD("integrator") PbrtEntity                             integrator;
    RL_FIELD("pixelFilter") PbrtEntity                             pixelFilter;
    RL_FIELD("accelerator") PbrtEntity                             accelerator;
    RL_FIELD("options") std::vector<PbrtOption>                options;
    RL_FIELD("materials") std::vector<AssetID<MaterialAsset>>    materials;
    RL_FIELD("textures") std::vector<AssetID<PbrtTextureAsset>> textures;
    RL_FIELD("shapes") std::vector<PbrtShape>                 shapes;
    RL_FIELD("worldShapes") std::vector<uint32_t>                  worldShapes;
    RL_FIELD("areaLights") std::vector<PbrtEntity>                areaLights;
    RL_FIELD("lights") std::vector<PbrtLight>                 lights;
    RL_FIELD("media") std::vector<AssetID<PbrtMediumAsset>> media;
    RL_FIELD("objects") std::vector<PbrtObjectDefinition>      objects;
    RL_FIELD("instances") std::vector<PbrtInstance>              instances;
    RL_FIELD("files") std::vector<PbrtFileReference>         files;

    bool operator==(const PbrtSceneAsset&) const = default;
};

#include RL_METADATA_HEADER("metadata/src/asset/pbrt/PbrtSceneAsset.generated.h")
