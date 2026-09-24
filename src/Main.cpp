#include "Engine.h"
#include "asset/AssetManager.h"
#include "asset/TextureImporter.h"
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
        CHECK(argc == 4, "usage: RenderLab --import-texture|--import-environmentmap <id> <source>");
        ConfigManager config;
        AssetManager assets;
        context().config = &config;
        context().assetManager = &assets;
        config.init();
        assets.init();
        std::filesystem::path source{argv[3]};
        if (source.is_relative())
        {
            source = config.paths().assets / source;
        }
        const std::string_view command{argv[1]};
        if (command == "--import-texture")
        {
            TextureImporter::importTexture(assets, argv[2], source);
        }
        else if (command == "--import-environmentmap")
        {
            TextureImporter::importEnvironmentMap(assets, argv[2], source);
        }
        else
        {
            LOG_FATAL("unknown import command: {}", command);
        }
        assets.shutdown();
        config.shutdown();
        context().assetManager = nullptr;
        context().config = nullptr;
        return 0;
    }
    Engine engine;
    engine.init();
    engine.run();
}
