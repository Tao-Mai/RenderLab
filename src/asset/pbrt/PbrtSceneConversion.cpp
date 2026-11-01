#include "asset/pbrt/PbrtSceneConversion.h"

#include "asset/SceneAsset.h"
#include "asset/pbrt/PbrtParameters.h"
#include "asset/pbrt/PbrtParameterReader.h"
#include "asset/pbrt/PbrtSceneAsset.h"
#include "scene/actor/ALight.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/component/EnvironmentLightComponent.h"
#include "scene/component/GoniometricLightComponent.h"
#include "scene/component/LightTypes.h"
#include "scene/component/MeshAreaLightComponent.h"
#include "scene/component/ProjectionLightComponent.h"
#include "scene/component/RenderComponent.h"

#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/matrix.hpp>
#include <utility>

namespace pbrt_import
{
namespace
{
glm::dvec3 point(const PbrtEntity& entity, std::string_view name, glm::dvec3 defaultValue)
{
    if (!findParameter(entity, name))
        return defaultValue;
    const auto values = arrayParameter<PbrtFloatValues>(entity, name, "point3");
    CHECK(values.size() == 3, "invalid PBRT light point '{}' at {}:{}", name,
          entity.location.file.string(), entity.location.line);
    return {values[0], values[1], values[2]};
}

// Construct an engine light frame whose -Z axis propagates from 'from' to 'to'.
glm::dmat4 lightFrame(const PbrtEntity& entity, glm::dvec3 defaultFrom, glm::dvec3 defaultTo)
{
    const auto from = point(entity, "from", defaultFrom);
    const auto to = point(entity, "to", defaultTo);
    const auto offset = from - to;
    CHECK(glm::dot(offset, offset) > 0.0, "coincident PBRT light from/to at {}:{}",
          entity.location.file.string(), entity.location.line);
    const auto z = glm::normalize(offset);
    const auto up = std::abs(z.y) < 0.999 ? glm::dvec3{0, 1, 0} : glm::dvec3{1, 0, 0};
    const auto x = glm::normalize(glm::cross(up, z));
    const auto y = glm::cross(z, x);
    return {glm::dvec4{x, 0}, glm::dvec4{y, 0}, glm::dvec4{z, 0}, glm::dvec4{from, 1}};
}

void setTransform(AActor& actor, const glm::dmat4& matrix, const PbrtSourceLocation& location)
{
    CHECK(actor.transform().setMatrix(glm::mat4{matrix}), "invalid PBRT actor transform at {}:{}",
          location.file.string(), location.line);
}

std::unique_ptr<AActor> lightActor(const PbrtLight& source, const PbrtSceneAsset& scene,
    const glm::dmat4& engineFromRender, ImageImporter& images)
{
    const auto& entity = source.entity;
    auto emission = lightEmission(entity, images);
    if (source.medium)
        emission.medium = scene.media[*source.medium];
    auto matrix = engineFromRender * source.renderFromLight.start;
    if (entity.type == "infinite")
    {
        auto actor = std::make_unique<AActor>();
        auto& light = actor->addDefaultComponent<EnvironmentLightComponent>();
        light.emission = std::move(emission);
        const auto portal = arrayParameter<PbrtFloatValues>(entity, "portal", "point3");
        CHECK(portal.empty() || portal.size() == 12, "PBRT environment portal requires four vertices at {}:{}",
              entity.location.file.string(), entity.location.line);
        for (size_t index = 0; index < portal.size(); index += 3)
        {
            const auto p = kEngineFromPbrtWorld * glm::dvec4{portal[index], portal[index + 1], portal[index + 2], 1};
            const glm::vec3 vertex{p};
            CHECK(std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z),
                  "PBRT portal exceeds engine precision at {}:{}", entity.location.file.string(), entity.location.line);
            light.portal.push_back(vertex);
        }
        setTransform(*actor, matrix, entity.location);
        return actor;
    }

    std::unique_ptr<LightComponent> component;
    if (entity.type == "point")
    {
        component = std::make_unique<PointLightComponent>();
        matrix *= glm::translate(glm::dmat4{1}, point(entity, "from", glm::dvec3{0}));
    }
    else if (entity.type == "spot")
    {
        auto spot = std::make_unique<SpotLightComponent>();
        constexpr double kDefaultConeAngle = 30.0;
        constexpr double kDefaultConeDeltaAngle = 5.0;
        const auto outer = scalarParameter<PbrtFloatValues>(entity, "coneangle", "float", kDefaultConeAngle);
        const auto delta = scalarParameter<PbrtFloatValues>(entity, "conedeltaangle", "float", kDefaultConeDeltaAngle);
        CHECK(std::isfinite(outer) && outer > 0.0 && outer < 180.0 &&
              std::isfinite(delta) && delta >= 0.0 && delta <= outer,
              "invalid PBRT spotlight cone at {}:{}", entity.location.file.string(), entity.location.line);
        spot->cosOuter = static_cast<float>(std::cos(glm::radians(outer)));
        spot->cosInner = static_cast<float>(std::cos(glm::radians(outer - delta)));
        matrix *= lightFrame(entity, glm::dvec3{0}, glm::dvec3{0, 0, 1});
        component = std::move(spot);
    }
    else if (entity.type == "distant")
    {
        component = std::make_unique<DirectionalLightComponent>();
        matrix *= lightFrame(entity, glm::dvec3{0}, glm::dvec3{0, 0, 1});
    }
    else if (entity.type == "projection")
    {
        auto projection = std::make_unique<ProjectionLightComponent>();
        const auto fov = scalarParameter<PbrtFloatValues>(entity, "fov", "float", projection->fieldOfView);
        CHECK(std::isfinite(fov) && fov > 0.0 && fov < 180.0, "invalid PBRT projection field of view at {}:{}",
              entity.location.file.string(), entity.location.line);
        projection->fieldOfView = static_cast<float>(fov);
        matrix *= glm::dmat4{glm::dvec4{-1, 0, 0, 0}, glm::dvec4{0, 1, 0, 0},
                            glm::dvec4{0, 0, -1, 0}, glm::dvec4{0, 0, 0, 1}};
        component = std::move(projection);
    }
    else if (entity.type == "goniometric")
        component = std::make_unique<GoniometricLightComponent>();
    else
    {
        CHECK(false, "unsupported PBRT light '{}' at {}:{}", entity.type,
              entity.location.file.string(), entity.location.line);
        std::unreachable();
    }
    component->offlineEmission = std::move(emission);
    auto actor = std::make_unique<ALight>(std::move(component));
    setTransform(*actor, matrix, entity.location);
    return actor;
}
}  // namespace

void populateScene(SceneAsset& scene, const PbrtSceneAsset& source, ImageImporter& images)
{
    const auto engineFromRender = kEngineFromPbrtWorld * glm::inverse(source.renderFromWorld);
    if (source.cameraMedium)
        scene.cameraMedium = source.media[*source.cameraMedium];
    const auto appendShape = [&](uint32_t shapeIndex, const glm::dmat4& instance)
    {
        const auto& shape = source.shapes[shapeIndex];
        DCHECK(shape.mesh);
        auto actor = std::make_unique<AStaticMesh>();
        actor->name = "shape" + std::to_string(scene.actors.size());
        auto& render = *actor->getComponent<RenderComponent>();
        render.meshId = *shape.mesh;
        render.offlineSurface.emplace();
        auto& surface = *render.offlineSurface;
        ParameterReader reader(shape.entity);
        surface.alpha = reader.floatInput("alpha", 1.0);
        surface.reverseOrientation = shape.reverseOrientation;
        if (shape.insideMedium) surface.insideMedium = source.media[*shape.insideMedium];
        if (shape.outsideMedium) surface.outsideMedium = source.media[*shape.outsideMedium];
        surface.faceIndices = shape.faceIndices;
        setTransform(*actor, engineFromRender * instance * shape.renderFromObject.start, shape.entity.location);
        if (shape.areaLight)
        {
            const auto& entity = source.areaLights[*shape.areaLight];
            CHECK(entity.type == "diffuse", "unsupported PBRT area light '{}' at {}:{}", entity.type,
                  entity.location.file.string(), entity.location.line);
            auto& light = actor->addDefaultComponent<MeshAreaLightComponent>();
            light.emission = lightEmission(entity, images);
            light.emission.medium = surface.outsideMedium;
            light.twoSided = scalarParameter<PbrtBoolValues>(entity, "twosided", "bool", false) != 0;
        }
        scene.actors.push_back(std::move(actor));
    };

    for (const auto shape : source.worldShapes)
        appendShape(shape, glm::dmat4{1});
    for (const auto& instance : source.instances)
        for (const auto shape : source.objects[instance.definition].shapes)
            appendShape(shape, instance.renderFromInstance.start);
    for (const auto& light : source.lights)
    {
        auto actor = lightActor(light, source, engineFromRender, images);
        actor->name = "light" + std::to_string(scene.actors.size());
        scene.actors.push_back(std::move(actor));
    }
}
}  // namespace pbrt_import
