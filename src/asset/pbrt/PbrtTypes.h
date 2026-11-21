#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/AssetID.h"

#include <cstdint>
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct MaterialAsset;
struct MeshAsset;
struct PbrtTextureAsset;
struct PbrtMediumAsset;

struct RL_RECORD PbrtSourceLocation
{
    RL_FIELD("file") std::filesystem::path file;
    RL_FIELD("line") int                   line   = 0;
    RL_FIELD("column") int                   column = 0;

    bool operator==(const PbrtSourceLocation&) const = default;
};

enum class RL_ENUM PbrtFileKind
{
    Source RL_ENUM_VALUE("Source"),
    Image RL_ENUM_VALUE("Image"),
    Geometry RL_ENUM_VALUE("Geometry"),
    Spectrum RL_ENUM_VALUE("Spectrum"),
    Ptex RL_ENUM_VALUE("Ptex"),
    MeasuredBrdf RL_ENUM_VALUE("MeasuredBrdf"),
    Lens RL_ENUM_VALUE("Lens"),
    Volume RL_ENUM_VALUE("Volume"),
    ReferenceImage RL_ENUM_VALUE("ReferenceImage")
};
struct RL_TYPE("PbrtFileReference") PbrtFileReference
{
    RL_FIELD("originalName") std::string           originalName;
    RL_FIELD("path") std::filesystem::path path;
    RL_FIELD("kind") PbrtFileKind          kind = PbrtFileKind::Source;
    RL_FIELD("size") uint64_t              size = 0;

    bool operator==(const PbrtFileReference&) const = default;
};

struct RL_TYPE("PbrtFloatValues") PbrtFloatValues
{
    RL_FIELD("values") std::vector<double> values;

    bool operator==(const PbrtFloatValues&) const = default;
};
struct RL_TYPE("PbrtIntegerValues") PbrtIntegerValues
{
    RL_FIELD("values") std::vector<int32_t> values;

    bool operator==(const PbrtIntegerValues&) const = default;
};
struct RL_TYPE("PbrtStringValues") PbrtStringValues
{
    RL_FIELD("values") std::vector<std::string> values;

    bool operator==(const PbrtStringValues&) const = default;
};
struct RL_TYPE("PbrtBoolValues") PbrtBoolValues
{
    RL_FIELD("values") std::vector<uint8_t> values;

    bool operator==(const PbrtBoolValues&) const = default;
};
enum class RL_ENUM PbrtArrayType
{
    Float64 RL_ENUM_VALUE("Float64"),
    Int32 RL_ENUM_VALUE("Int32"),
    Bool8 RL_ENUM_VALUE("Bool8")
};
struct RL_TYPE("PbrtArrayReference") PbrtArrayReference
{
    RL_FIELD("path") std::filesystem::path path;
    RL_FIELD("offset") uint64_t              offset = 0;
    RL_FIELD("count") uint64_t              count  = 0;
    RL_FIELD("type") PbrtArrayType         type   = PbrtArrayType::Float64;

    bool operator==(const PbrtArrayReference&) const = default;
};
using PbrtParameterValue =
    std::variant<PbrtFloatValues, PbrtIntegerValues, PbrtStringValues, PbrtBoolValues, PbrtArrayReference>;

struct RL_TYPE("PbrtMaterialReference") PbrtMaterialReference
{
    RL_FIELD("id") AssetID<MaterialAsset> id;

    bool operator==(const PbrtMaterialReference&) const = default;
};
struct RL_TYPE("PbrtTextureReference") PbrtTextureReference
{
    RL_FIELD("id") AssetID<PbrtTextureAsset> id;

    bool operator==(const PbrtTextureReference&) const = default;
};
struct RL_RECORD PbrtParameterReference
{
    using Target = std::variant<PbrtFileReference, PbrtMaterialReference, PbrtTextureReference>;

    RL_FIELD("valueIndex") uint32_t valueIndex = 0;
    RL_FIELD("target") Target   target;

    bool operator==(const PbrtParameterReference&) const = default;
};
struct RL_RECORD PbrtParameter
{
    RL_FIELD("type") std::string                         type;
    RL_FIELD("name") std::string                         name;
    RL_FIELD("colorSpace") std::string                         colorSpace;
    RL_FIELD("location") PbrtSourceLocation                  location;
    RL_FIELD("value") PbrtParameterValue                  value;
    RL_FIELD("references") std::vector<PbrtParameterReference> references;

    bool operator==(const PbrtParameter&) const = default;
};
struct RL_RECORD PbrtEntity
{
    RL_FIELD("type") std::string                type;
    RL_FIELD("colorSpace") std::string                colorSpace;
    RL_FIELD("parameters") std::vector<PbrtParameter> parameters;
    RL_FIELD("location") PbrtSourceLocation         location;

    bool operator==(const PbrtEntity&) const = default;
};
struct RL_RECORD PbrtTransform
{
    RL_FIELD("start") glm::dmat4 start{1.0};
    RL_FIELD("end") glm::dmat4 end{1.0};
    RL_FIELD("startTime") double     startTime = 0;
    RL_FIELD("endTime") double     endTime   = 1;

    bool operator==(const PbrtTransform&) const = default;
};
struct RL_RECORD PbrtShape
{
    RL_FIELD("entity") PbrtEntity                            entity;
    RL_FIELD("renderFromObject") PbrtTransform                         renderFromObject;
    RL_FIELD("reverseOrientation") bool                                  reverseOrientation = false;
    RL_FIELD("material") std::optional<AssetID<MaterialAsset>> material;
    RL_FIELD("areaLight") std::optional<uint32_t>               areaLight;
    RL_FIELD("insideMedium") std::optional<uint32_t>               insideMedium;
    RL_FIELD("outsideMedium") std::optional<uint32_t>               outsideMedium;
    RL_FIELD("mesh") std::optional<AssetID<MeshAsset>>     mesh;
    RL_FIELD("faceIndices") std::optional<std::variant<PbrtIntegerValues, PbrtArrayReference>> faceIndices;

    bool operator==(const PbrtShape&) const = default;
};
struct RL_RECORD PbrtLight
{
    RL_FIELD("entity") PbrtEntity              entity;
    RL_FIELD("renderFromLight") PbrtTransform           renderFromLight;
    RL_FIELD("medium") std::optional<uint32_t> medium;

    bool operator==(const PbrtLight&) const = default;
};
struct RL_RECORD PbrtObjectDefinition
{
    RL_FIELD("name") std::string           name;
    RL_FIELD("location") PbrtSourceLocation    location;
    RL_FIELD("shapes") std::vector<uint32_t> shapes;

    bool operator==(const PbrtObjectDefinition&) const = default;
};
struct RL_RECORD PbrtInstance
{
    RL_FIELD("definition") uint32_t           definition = 0;
    RL_FIELD("renderFromInstance") PbrtTransform      renderFromInstance;
    RL_FIELD("location") PbrtSourceLocation location;

    bool operator==(const PbrtInstance&) const = default;
};
struct RL_RECORD PbrtOption
{
    RL_FIELD("name") std::string                      name;
    RL_FIELD("value") std::string                      value;
    RL_FIELD("location") PbrtSourceLocation               location;
    RL_FIELD("inputFile") std::optional<PbrtFileReference> inputFile;

    bool operator==(const PbrtOption&) const = default;
};

#include RL_METADATA_HEADER("metadata/src/asset/pbrt/PbrtTypes.generated.h")
