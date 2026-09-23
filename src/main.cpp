#include "engine.h"
#include "asset/asset_manager.h"
#include "asset/texture_importer.h"
#include "core/config_manager.h"
#include "core/context.h"
#include "core/logger.h"

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
        context().assets = &assets;
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
        context().assets = nullptr;
        context().config = nullptr;
        return 0;
    }
    Engine engine;
    engine.init();
    engine.run();
}
