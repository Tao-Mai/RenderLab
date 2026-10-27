#include "upstream/Scene.h"
#include "upstream/utf8proc/utf8proc.h"

namespace
{
PbrtSourceLocation sourceLocation(const pbrt::FileLoc& value) { return {value.filename, value.line, value.column}; }

void validateMatrix(const glm::dmat4& matrix, const pbrt::FileLoc& location)
{
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]))
                pbrt::ErrorExit(&location, "non-finite transform");
    const auto determinant = glm::determinant(matrix);
    if (!std::isfinite(determinant) || determinant == 0)
        pbrt::ErrorExit(&location, "non-invertible transform");
}

PbrtTransform transform(const pbrt::AnimatedTransform& value, const pbrt::FileLoc& location)
{
    validateMatrix(value.startTransform.matrix, location);
    validateMatrix(value.endTransform.matrix, location);
    if (value.startTime > value.endTime)
        pbrt::ErrorExit(&location, "transform start time exceeds end time");
    return {value.startTransform.matrix, value.endTransform.matrix, value.startTime, value.endTime};
}
PbrtTransform transform(const pbrt::Transform& value, const pbrt::FileLoc& location)
{
    validateMatrix(value.matrix, location);
    return {value.matrix, value.matrix, 0, 1};
}

PbrtEntity entity(const pbrt::SceneEntity& source)
{
    const auto* entityColorSpace = source.parameters.ColorSpace();
    PbrtEntity  result{source.name, entityColorSpace ? entityColorSpace->name : "srgb", {}, sourceLocation(source.loc)};
    for (const auto* input : source.parameters.GetParameterVector())
    {
        PbrtParameter parameter;
        parameter.type         = input->type;
        parameter.name         = input->name;
        parameter.location     = sourceLocation(input->loc);
        const auto* colorSpace = input->colorSpace ? input->colorSpace : source.parameters.ColorSpace();
        parameter.colorSpace   = colorSpace ? colorSpace->name : "srgb";
        if (input->type == "bool")
        {
            if (input->bools.empty())
                pbrt::ErrorExit(&input->loc, "non-Boolean parameter: %s", input->name);
            parameter.value = PbrtBoolValues{input->bools};
        }
        else if (input->type == "integer")
        {
            if (input->ints.empty())
                pbrt::ErrorExit(&input->loc, "non-integer parameter: %s", input->name);
            parameter.value = PbrtIntegerValues{input->ints};
        }
        else if (input->type == "string" || input->type == "texture" ||
                 (input->type == "spectrum" && !input->strings.empty()))
        {
            if (input->strings.empty())
                pbrt::ErrorExit(&input->loc, "non-string parameter: %s", input->name);
            parameter.value = PbrtStringValues{input->strings};
        }
        else
        {
            static const std::set<std::string> kNumericTypes{
                "float",
                "point2",
                "vector2",
                "point3",
                "vector3",
                "normal3",
                "rgb",
                "spectrum",
                "blackbody"
            };
            if (!kNumericTypes.contains(input->type) || input->floats.empty())
                pbrt::ErrorExit(&input->loc, "invalid parameter type/value: %s %s", input->type, input->name);
            for (auto value : input->floats)
                if (!std::isfinite(value))
                    pbrt::ErrorExit(&input->loc, "non-finite parameter: %s", input->name);
            size_t width = (input->type == "point2" || input->type == "vector2" || input->type == "spectrum") ? 2
                : (input->type == "point3" || input->type == "vector3" || input->type == "normal3" ||
                   input->type == "rgb")
                ? 3
                : 1;
            if (input->floats.size() % width)
                pbrt::ErrorExit(&input->loc, "invalid parameter array size: %s", input->name);
            parameter.value = PbrtFloatValues{input->floats};
        }
        result.parameters.push_back(std::move(parameter));
    }
    return result;
}
std::string stringParameter(const PbrtEntity& entity, const std::string& name)
{
    for (const auto& parameter : entity.parameters)
        if (parameter.name == name && parameter.type == "string")
            if (auto* strings = std::get_if<PbrtStringValues>(&parameter.value); strings && strings->values.size() == 1)
                return strings->values.front();
    pbrt::ErrorExit("required string parameter '%s' at %s", name, entity.location.file.string());
}
}  // namespace

namespace pbrt_import
{
std::optional<std::string> normalizeResourceName(const std::string& name)
{
    auto* normalized = utf8proc_NFC(reinterpret_cast<const utf8proc_uint8_t*>(name.c_str()));
    if (!normalized)
        return {};
    std::string result(reinterpret_cast<const char*>(normalized));
    std::free(normalized);
    return result;
}

bool isNamedSpectrum(const std::string& name)
{
    static const std::set<std::string> kNames{
#include "upstream/NamedSpectra.inc"
    };
    return kNames.contains(name);
}

ReadResult readScene(const std::filesystem::path& source, const Diagnostic& diagnostic)
{
    // Keep parsing sessions serialized, including the upstream counters and scoped host state.
    static std::mutex readMutex;
    std::lock_guard   lock(readMutex);
    pbrt::Session     session;
    session.diagnostic = diagnostic;
    pbrt::session      = &session;
    pbrt::Options      = &session.options;
    ReadResult result;
    {
        pbrt::BasicScene        collected;
        pbrt::BasicSceneBuilder builder(&collected);
        const std::string       filenames[]{source.string()};
        pbrt::ParseFiles(&builder, filenames);
        auto& scene           = result.scene;
        scene.readerRevision  = kReaderRevision;
        scene.renderFromWorld = builder.RenderFromWorldForExport().matrix;
        validateMatrix(scene.renderFromWorld, collected.camera.loc);
        scene.renderingSpace  = session.options.renderingSpace == pbrt::RenderingCoordinateSystem::World ? "world"
             : session.options.renderingSpace == pbrt::RenderingCoordinateSystem::Camera                 ? "camera"
                                                                                         : "cameraworld";
        scene.camera          = entity(collected.camera);
        scene.worldFromCamera = transform(collected.camera.cameraTransform.worldFromCamera, collected.camera.loc);
        scene.film            = entity(collected.film);
        scene.sampler         = entity(collected.sampler);
        scene.integrator      = entity(collected.integrator);
        scene.pixelFilter     = entity(collected.filter);
        scene.accelerator     = entity(collected.accelerator);
        scene.options         = std::move(collected.options);

        std::map<std::string, uint32_t> media;
        for (const auto& input : collected.media)
        {
            auto data = entity(input);
            data.type = stringParameter(data, "type");
            if (!media.emplace(input.name, static_cast<uint32_t>(scene.media.size())).second)
                pbrt::ErrorExit(&input.loc, "duplicate medium: %s", input.name);
            scene.media.push_back({input.name, std::move(data), transform(input.renderFromObject, input.loc)});
        }
        const auto mediumIndex = [&](const std::string& name, const pbrt::FileLoc& location) -> std::optional<uint32_t>
        {
            if (name.empty())
                return {};
            auto found = media.find(name);
            if (found == media.end())
                pbrt::ErrorExit(&location, "undefined medium: %s", name);
            return found->second;
        };
        scene.cameraMedium = mediumIndex(collected.camera.medium, collected.camera.loc);
        std::map<std::string, uint32_t> namedMaterials;
        for (const auto& [name, input] : collected.materials)
        {
            auto data = entity(input);
            if (!name.empty())
            {
                data.type = stringParameter(data, "type");
                if (!namedMaterials.emplace(name, static_cast<uint32_t>(result.materials.size())).second)
                    pbrt::ErrorExit(&input.loc, "duplicate material: %s", name);
            }
            result.materials.push_back({name, std::move(data)});
        }
        const auto appendShape = [&](const auto& input)
        {
            PbrtShape shape;
            shape.entity = entity(input);
            if constexpr (requires { input.renderFromObject.startTransform; })
                shape.renderFromObject = transform(input.renderFromObject, input.loc);
            else
                shape.renderFromObject = transform(*input.renderFromObject, input.loc);
            shape.reverseOrientation = input.reverseOrientation;
            int material             = input.materialIndex;
            if (!input.materialName.empty())
            {
                auto found = namedMaterials.find(input.materialName);
                if (found == namedMaterials.end())
                    pbrt::ErrorExit(&input.loc, "undefined material: %s", input.materialName);
                material = static_cast<int>(found->second);
            }
            if (input.lightIndex >= 0)
                shape.areaLight = static_cast<uint32_t>(input.lightIndex);
            shape.insideMedium  = mediumIndex(input.insideMedium, input.loc);
            shape.outsideMedium = mediumIndex(input.outsideMedium, input.loc);
            auto index          = static_cast<uint32_t>(scene.shapes.size());
            result.shapeMaterialIndices.push_back(material >= 0 ? std::optional<uint32_t>(material) : std::nullopt);
            scene.shapes.push_back(std::move(shape));
            return index;
        };
        for (const auto& input : collected.areaLights)
            scene.areaLights.push_back(entity(input));
        for (const auto& input : collected.lights)
            scene.lights.push_back(
                {entity(input), transform(input.renderFromObject, input.loc), mediumIndex(input.medium, input.loc)}
            );
        for (const auto& input : collected.shapes)
            scene.worldShapes.push_back(appendShape(input));
        for (const auto& input : collected.animatedShapes)
            scene.worldShapes.push_back(appendShape(input));
        std::map<std::string, uint32_t> definitions;
        for (const auto& input : collected.definitions)
        {
            PbrtObjectDefinition definition{input.name, sourceLocation(input.loc), {}};
            for (const auto& shape : input.shapes)
                definition.shapes.push_back(appendShape(shape));
            for (const auto& shape : input.animatedShapes)
                definition.shapes.push_back(appendShape(shape));
            if (!definitions.emplace(input.name, static_cast<uint32_t>(scene.objects.size())).second)
                pbrt::ErrorExit(&input.loc, "duplicate object: %s", input.name);
            scene.objects.push_back(std::move(definition));
        }
        for (auto& input : collected.instances)
        {
            auto found = definitions.find(input.name);
            if (found == definitions.end())
                pbrt::ErrorExit(&input.loc, "undefined object: %s", input.name);
            auto matrix = input.renderFromInstanceAnim ? transform(*input.renderFromInstanceAnim, input.loc)
                                                       : transform(*input.renderFromInstance, input.loc);
            scene.instances.push_back({found->second, matrix, sourceLocation(input.loc)});
            delete input.renderFromInstanceAnim;
            input.renderFromInstanceAnim = nullptr;
        }
        const auto appendTextures = [&](const auto& inputs, PbrtTextureType type)
        {
            for (const auto& [name, input] : inputs)
            {
                PbrtTextureAsset texture;
                texture.name              = name;
                texture.valueType         = type;
                texture.entity            = entity(input);
                texture.renderFromTexture = transform(input.renderFromObject, input.loc);
                result.textures.push_back(std::move(texture));
            }
        };
        appendTextures(collected.floatTextures, PbrtTextureType::Float);
        appendTextures(collected.spectrumTextures, PbrtTextureType::Spectrum);
        result.sourceFiles = session.sourceFiles;
    }
    pbrt::session = nullptr;
    pbrt::Options = nullptr;
    return result;
}
}  // namespace pbrt_import
