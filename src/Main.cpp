#include "Engine.h"
#include "asset/AssetImporter.h"
#include "asset/AssetManager.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
constexpr std::string_view kImportUsage =
    "usage: RenderLab --import-texture|--import-environmentmap|--import-mesh|--import-scene "
    "<source> [--option value ...]";

class ImportOptions
{
public:
    explicit ImportOptions(std::span<char* const> arguments)
    {
        for (size_t index = 0; index < arguments.size(); index += 2)
        {
            const std::string_view option = arguments[index];
            const std::string_view value  = arguments[index + 1];
            CHECK(option.starts_with("--"), "invalid import option '{}'", option);
            CHECK(!value.empty(), "missing value for import option '{}'", option);
            const bool inserted = values.emplace(option, value).second;
            CHECK(inserted, "duplicate import option '{}'", option);
        }
    }

    std::optional<std::string_view> take(std::string_view option)
    {
        const auto entry = values.extract(option);
        if (entry.empty())
            return std::nullopt;
        return entry.mapped();
    }

    template <class T>
    std::optional<T> takeNumber(std::string_view option)
    {
        const auto value = take(option);
        if (!value)
            return std::nullopt;

        T number{};
        const auto parsed = std::from_chars(value->data(), value->data() + value->size(), number);
        CHECK(
            parsed.ec == std::errc{} && parsed.ptr == value->data() + value->size(),
            "invalid value '{}' for {}",
            *value,
            option
        );
        return number;
    }

    void checkEmpty() const { CHECK(values.empty(), "unsupported import option '{}'", values.begin()->first); }

private:
    std::map<std::string_view, std::string_view> values;
};

std::optional<ColorSpace> takeColorSpace(ImportOptions& options)
{
    const auto value = options.take("--color-space").value_or("auto");
    if (value == "auto")
        return std::nullopt;

    CHECK(value == "linear" || value == "srgb", "invalid color space '{}'", value);
    return value == "linear" ? ColorSpace::Linear : ColorSpace::Srgb;
}

std::string importAsset(
    std::string_view command, const std::filesystem::path& source, ImportOptions& options
)
{
    if (command == "--import-texture")
    {
        const auto colorSpace = takeColorSpace(options);
        options.checkEmpty();
        return AssetImporter::importTexture({.source = source, .colorSpace = colorSpace}).value;
    }
    if (command == "--import-environmentmap")
    {
        ImportEnvironmentMapSetting setting{.source = source, .colorSpace = takeColorSpace(options)};
        options.checkEmpty();
        return AssetImporter::importEnvironmentMap(setting).value;
    }
    if (command == "--import-mesh" || command == "--import-scene")
    {
        const std::string id{options.take("--id").value_or("")};
        const auto scale = options.takeNumber<float>("--scale");
        if (command == "--import-scene")
        {
            ImportSceneSetting setting{.source = source, .id = id};
            setting.importScale = scale.value_or(setting.importScale);
            const auto kind = options.take("--kind").value_or("environment");
            CHECK(kind == "environment" || kind == "model-preview", "invalid scene kind '{}'", kind);
            setting.kind = kind == "environment" ? Scene::Kind::Environment : Scene::Kind::ModelPreview;
            options.checkEmpty();
            return AssetImporter::importScene(setting).value;
        }

        ImportMeshSetting setting{.source = source, .id = id};
        setting.importScale   = scale.value_or(setting.importScale);
        const auto targetSide = options.takeNumber<float>("--longest-side");
        options.checkEmpty();
        if (!targetSide)
            return AssetImporter::importMesh(setting).value;

        CHECK(!scale, "--scale and --longest-side cannot be combined");
        CHECK(std::isfinite(*targetSide) && *targetSide > 0.0f, "--longest-side must be finite and positive");
        auto prepared = AssetImporter::prepareMesh(source);
        const auto size = prepared.boundsMax - prepared.boundsMin;
        const float longestSide = std::max({size.x, size.y, size.z});
        CHECK(std::isfinite(longestSide) && longestSide > 0.0f, "mesh has invalid dimensions: {}", source.string());

        setting.importScale = *targetSide / longestSide;
        return AssetImporter::saveMesh(std::move(prepared), setting).value;
    }
    LOG_FATAL("unknown import command '{}'. {}", command, kImportUsage);
}
}  // namespace

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    if (argc > 1)
    {
        CHECK(argc >= 3 && (argc - 3) % 2 == 0, "{}", kImportUsage);
        ImportOptions options{std::span<char* const>{argv + 3, static_cast<size_t>(argc - 3)}};

        AssetManager assets;
        context().assetManager = &assets;

        Engine::loadConfig();
        const auto assetRoot = Engine::assetRoot();
        assets.init(assetRoot);

        std::filesystem::path source{argv[2]};
        CHECK(!source.empty(), "asset source path is empty");
        if (source.is_relative())
        {
            source = assetRoot.parent_path() / source;
        }
        const auto importedId = importAsset(argv[1], source, options);
        LOG_INFO("imported asset ID: {}", importedId);
        assets.shutdown();
        context().assetManager = nullptr;

        return 0;
    }
    Engine engine;
    engine.init();
    engine.run();
    engine.shutdown();
}
