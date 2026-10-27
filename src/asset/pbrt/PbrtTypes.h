#pragma once

#include "asset/AssetID.h"

#include <cstdint>
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct MaterialAsset;
struct PbrtTextureAsset;

struct PbrtSourceLocation
{
    std::filesystem::path file;
    int                   line   = 0;
    int                   column = 0;

    bool operator==(const PbrtSourceLocation&) const = default;
};

enum class PbrtFileKind
{
    Source,
    Image,
    Geometry,
    Spectrum,
    Ptex,
    MeasuredBrdf,
    Lens,
    Volume,
    ReferenceImage
};
struct PbrtFileReference
{
    std::string           originalName;
    std::filesystem::path path;
    PbrtFileKind          kind = PbrtFileKind::Source;
    uint64_t              size = 0;

    bool operator==(const PbrtFileReference&) const = default;
};

struct PbrtFloatValues
{
    std::vector<double> values;

    bool operator==(const PbrtFloatValues&) const = default;
};
struct PbrtIntegerValues
{
    std::vector<int32_t> values;

    bool operator==(const PbrtIntegerValues&) const = default;
};
struct PbrtStringValues
{
    std::vector<std::string> values;

    bool operator==(const PbrtStringValues&) const = default;
};
struct PbrtBoolValues
{
    std::vector<uint8_t> values;

    bool operator==(const PbrtBoolValues&) const = default;
};
enum class PbrtArrayType
{
    Float64,
    Int32,
    Bool8
};
struct PbrtArrayReference
{
    std::filesystem::path path;
    uint64_t              offset = 0;
    uint64_t              count  = 0;
    PbrtArrayType         type   = PbrtArrayType::Float64;

    bool operator==(const PbrtArrayReference&) const = default;
};
using PbrtParameterValue =
    std::variant<PbrtFloatValues, PbrtIntegerValues, PbrtStringValues, PbrtBoolValues, PbrtArrayReference>;

struct PbrtMaterialReference
{
    AssetID<MaterialAsset> id;

    bool operator==(const PbrtMaterialReference&) const = default;
};
struct PbrtTextureReference
{
    AssetID<PbrtTextureAsset> id;

    bool operator==(const PbrtTextureReference&) const = default;
};
struct PbrtParameterReference
{
    using Target = std::variant<PbrtFileReference, PbrtMaterialReference, PbrtTextureReference>;

    uint32_t valueIndex = 0;
    Target   target;

    bool operator==(const PbrtParameterReference&) const = default;
};
struct PbrtParameter
{
    std::string                         type;
    std::string                         name;
    std::string                         colorSpace;
    PbrtSourceLocation                  location;
    PbrtParameterValue                  value;
    std::vector<PbrtParameterReference> references;

    bool operator==(const PbrtParameter&) const = default;
};
struct PbrtEntity
{
    std::string                type;
    std::string                colorSpace;
    std::vector<PbrtParameter> parameters;
    PbrtSourceLocation         location;

    bool operator==(const PbrtEntity&) const = default;
};
struct PbrtTransform
{
    glm::dmat4 start{1.0};
    glm::dmat4 end{1.0};
    double     startTime = 0;
    double     endTime   = 1;

    bool operator==(const PbrtTransform&) const = default;
};
struct PbrtShape
{
    PbrtEntity                            entity;
    PbrtTransform                         renderFromObject;
    bool                                  reverseOrientation = false;
    std::optional<AssetID<MaterialAsset>> material;
    std::optional<uint32_t>               areaLight;
    std::optional<uint32_t>               insideMedium;
    std::optional<uint32_t>               outsideMedium;

    bool operator==(const PbrtShape&) const = default;
};
struct PbrtMedium
{
    std::string   name;
    PbrtEntity    entity;
    PbrtTransform renderFromMedium;

    bool operator==(const PbrtMedium&) const = default;
};
struct PbrtLight
{
    PbrtEntity              entity;
    PbrtTransform           renderFromLight;
    std::optional<uint32_t> medium;

    bool operator==(const PbrtLight&) const = default;
};
struct PbrtObjectDefinition
{
    std::string           name;
    PbrtSourceLocation    location;
    std::vector<uint32_t> shapes;

    bool operator==(const PbrtObjectDefinition&) const = default;
};
struct PbrtInstance
{
    uint32_t           definition = 0;
    PbrtTransform      renderFromInstance;
    PbrtSourceLocation location;

    bool operator==(const PbrtInstance&) const = default;
};
struct PbrtOption
{
    std::string                      name;
    std::string                      value;
    PbrtSourceLocation               location;
    std::optional<PbrtFileReference> inputFile;

    bool operator==(const PbrtOption&) const = default;
};
