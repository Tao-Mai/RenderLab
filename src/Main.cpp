#include "Engine.h"
#include "asset/AssetDescManager.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <filesystem>
#include <string_view>

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    if (argc > 1)
    {
        CHECK(argc == 3,
              "usage: RenderLab --import-texture|--import-environmentmap|--import-mesh <source>");
        ConfigManager config;
        AssetDescManager assets;
        AssetDataManager assetData;
        context().config = &config;
        context().assetManager = &assets;
        context().assetDataManager = &assetData;
        config.init();
        assetData.init();
        context().assetManager->init();
        std::filesystem::path source{argv[2]};
        if (source.is_relative())
        {
            source = config.paths().assets.parent_path() / source;
        }
        const std::string_view command{argv[1]};
        AssetId importedId;
        if (command == "--import-texture")
        {
            importedId = AssetImporter::importTexture(source);
        }
        else if (command == "--import-environmentmap")
        {
            importedId = AssetImporter::importEnvironmentMap(source);
        }
        else if (command == "--import-mesh")
        {
            importedId = AssetImporter::importMesh(source);
        }
        else
        {
            LOG_FATAL("unknown import command: {}", command);
        }
        LOG_INFO("imported asset ID: {}", importedId);
        context().assetManager->shutdown();
        config.shutdown();
        context().assetManager = nullptr;
        context().assetDataManager = nullptr;
        context().config = nullptr;
        return 0;
    }
    Engine engine;
    engine.init();
    engine.run();
}
