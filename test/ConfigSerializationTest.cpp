#include "core/Serializer.h"
#include "core/ConfigManager.h"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace
{
enum class [[=Flags{}]] TestFlags : unsigned int
{
    None = 0, First = 1, Second = 2, Third = 4, All = 7,
};

enum class OrdinaryEnum : unsigned int
{
    First = 1, Second = 2, Third = 4,
};

static_assert(std::meta::annotations_of_with_type(^^Modifier, ^^Flags).size() == 1);
static_assert(std::meta::annotations_of_with_type(^^OrdinaryEnum, ^^Flags).empty());

using TestFlagsAlias = TestFlags;
static_assert(std::meta::annotations_of_with_type(std::meta::dealias(^^TestFlagsAlias), ^^Flags).size() == 1);

AppConfig sampleConfig()
{
    return {
        .paths = {.assets = "../assets", .commands = "commands.json", .editor = "editor.json",
            .renderer = "renderer.json"},
        .initialScene = SceneAsset::ID{"default"},
    };
}

RendererConfig sampleRendererConfig()
{
    return {
        .renderPath = RenderPath::RayTracing,
        .brdfLut = {TextureAsset::ID{"lut_ggx"}, SamplerAsset::ID{"linearClamp"}},
        .feature = {.ibl = false, .ambientCoefficient = 0.25f,
            .shadow = {.enable = false, .pointShadowMapSize = 2048, .bias = 0.0025f,
                .filter = SamplerAsset::Filter::Nearest, .pixelRotation = false, .sampleRadius = 0.03f}},
        .environmentMap = EnvironmentMapAsset::ID{"grasslands"},
    };
}
}

TEST(ConfigSerialization, UsesMemberNamesAndReflectedAssetIds)
{
    const json serialized = Serialize(sampleConfig());

    EXPECT_EQ(serialized.at("paths").at("assets"), "../assets");
    EXPECT_EQ(serialized.at("paths").at("commands"), "commands.json");
    EXPECT_EQ(serialized.at("paths").at("editor"), "editor.json");
    EXPECT_EQ(serialized.at("paths").at("renderer"), "renderer.json");
    EXPECT_EQ(serialized.at("initialScene").at("value"), "default");
    EXPECT_FALSE(serialized.contains("renderer"));
}

TEST(ConfigSerialization, RoundTripsNestedConfig)
{
    const AppConfig original = sampleConfig();
    AppConfig restored;
    Deserialize(json::parse(Serialize(original).dump()), restored);

    EXPECT_EQ(restored.paths.assets, original.paths.assets);
    EXPECT_EQ(restored.paths.commands, original.paths.commands);
    EXPECT_EQ(restored.paths.editor, original.paths.editor);
    EXPECT_EQ(restored.paths.renderer, original.paths.renderer);
    EXPECT_EQ(restored.initialScene, original.initialScene);
}

TEST(ConfigSerialization, RendererConfigRoundTripsExplicitFeatures)
{
    const auto original = sampleRendererConfig();
    const json serialized = Serialize(original);
    EXPECT_EQ(serialized.at("renderPath"), "RayTracing");
    EXPECT_EQ(serialized.at("brdfLut").at("textureID").at("value"), "lut_ggx");
    EXPECT_EQ(serialized.at("brdfLut").at("samplerID").at("value"), "linearClamp");
    EXPECT_EQ(serialized.at("feature").at("ibl"), false);
    EXPECT_FLOAT_EQ(serialized.at("feature").at("ambientCoefficient").get<float>(), 0.25f);
    EXPECT_EQ(serialized.at("environmentMap").at("value"), "grasslands");
    EXPECT_EQ(serialized.at("feature").at("shadow").at("enable"), false);
    EXPECT_EQ(serialized.at("feature").at("shadow").at("pointShadowMapSize"), 2048);
    EXPECT_EQ(serialized.at("feature").at("shadow").at("rectAreaShadowMapSize"), 1024);
    EXPECT_FLOAT_EQ(serialized.at("feature").at("shadow").at("bias").get<float>(), 0.0025f);
    EXPECT_EQ(serialized.at("feature").at("shadow").at("filter"), "Nearest");
    EXPECT_EQ(serialized.at("feature").at("shadow").at("pixelRotation"), false);
    EXPECT_FLOAT_EQ(serialized.at("feature").at("shadow").at("sampleRadius").get<float>(), 0.03f);

    RendererConfig restored;
    Deserialize(serialized, restored);
    EXPECT_EQ(Serialize(restored), serialized);
}

TEST(ConfigSerialization, MissingFieldsFailInsteadOfUsingDefaults)
{
    json serialized = Serialize(sampleConfig());
    serialized.at("paths").erase("commands");
    AppConfig restored;
    EXPECT_DEATH(Deserialize(serialized, restored), "");

    RendererConfig restoredRenderer;
    serialized = Serialize(sampleRendererConfig());
    serialized.at("brdfLut").at("samplerID").erase("value");
    EXPECT_DEATH(Deserialize(serialized, restoredRenderer), "");

    serialized = Serialize(sampleRendererConfig());
    serialized.at("feature").erase("ibl");
    EXPECT_DEATH(Deserialize(serialized, restoredRenderer), "");

    serialized = Serialize(sampleRendererConfig());
    serialized.at("feature").at("shadow").erase("bias");
    EXPECT_DEATH(Deserialize(serialized, restoredRenderer), "");

    serialized = Serialize(sampleRendererConfig());
    serialized.at("feature").at("shadow").erase("filter");
    EXPECT_DEATH(Deserialize(serialized, restoredRenderer), "");

    serialized = Serialize(sampleRendererConfig());
    serialized.at("feature").at("shadow").erase("pixelRotation");
    EXPECT_DEATH(Deserialize(serialized, restoredRenderer), "");

    serialized = Serialize(sampleRendererConfig());
    serialized.at("feature").at("shadow").erase("sampleRadius");
    EXPECT_DEATH(Deserialize(serialized, restoredRenderer), "");
}

TEST(ConfigSerialization, RejectsInvalidPathType)
{
    json serialized = Serialize(sampleConfig());
    serialized.at("paths").at("assets") = 42;
    AppConfig restored;
    EXPECT_DEATH(Deserialize(serialized, restored), "");
}

TEST(ConfigSerialization, ConfigManagerResolvesPathsAndLoadsCommands)
{
    const auto configFile = std::filesystem::path{RENDERLAB_CONFIG_FILE};
    const auto originalConfig = asset_json::load<AppConfig>(configFile);
    ConfigManager manager;
    manager.init();

    const auto source = std::filesystem::path{RENDERLAB_SOURCE_DIR};
    EXPECT_EQ(std::filesystem::weakly_canonical(manager.paths().assets), source / "test");
    EXPECT_EQ(manager.paths().commands, source / "config" / "commands.json");
    EXPECT_EQ(manager.paths().editor, source / "config" / "editor.json");
    EXPECT_EQ(manager.paths().renderer, source / "test" / "renderer.json");
    EXPECT_EQ(Serialize(manager.rendererConfig()),
        Serialize(asset_json::load<RendererConfig>(manager.paths().renderer)));
    const auto rendererFile = manager.paths().renderer;
    const auto originalRenderer = manager.rendererConfig();
    manager.setRenderPath(RenderPath::RayTracing);
    manager.setEnvironmentMap(EnvironmentMapAsset::ID{"grasslands"});
    manager.setRenderFeature({.ibl = false, .ambientCoefficient = 0.3f,
        .shadow = {.enable = false, .pointShadowMapSize = 512, .bias = 0.005f,
            .filter = SamplerAsset::Filter::Nearest, .pixelRotation = false, .sampleRadius = 0.04f}});
    EXPECT_FALSE(manager.rendererConfig().feature.ibl);
    EXPECT_FLOAT_EQ(manager.rendererConfig().feature.ambientCoefficient, 0.3f);
    EXPECT_FLOAT_EQ(manager.rendererConfig().feature.shadow.bias, 0.005f);
    EXPECT_EQ(manager.rendererConfig().feature.shadow.filter, SamplerAsset::Filter::Nearest);
    EXPECT_FALSE(manager.rendererConfig().feature.shadow.pixelRotation);
    EXPECT_FLOAT_EQ(manager.rendererConfig().feature.shadow.sampleRadius, 0.04f);
    EXPECT_EQ(Serialize(asset_json::load<RendererConfig>(rendererFile)), Serialize(originalRenderer));
    const auto editor = asset_json::load<EditorConfig>(manager.paths().editor);
    EXPECT_EQ(manager.editorConfig().width, editor.width);
    EXPECT_EQ(manager.editorConfig().height, editor.height);
    EXPECT_EQ(manager.editorConfig().title, editor.title);
    EXPECT_EQ(manager.initialScene(), SceneAsset::ID{"default"});
    manager.setInitialScene(SceneAsset::ID{"selected-scene"});
    EXPECT_EQ(manager.initialScene(), SceneAsset::ID{"selected-scene"});
    EXPECT_EQ(Serialize(asset_json::load<AppConfig>(configFile)), Serialize(originalConfig));
    EXPECT_EQ(manager.commandConfig().bindings.size(), 16);
    EXPECT_EQ(manager.commandConfig().bindings.front().command, Command::Quit);
    EXPECT_EQ(manager.commandConfig().bindings.at(12).command, Command::SaveScene);
    EXPECT_EQ(manager.commandConfig().bindings.at(12).modifiers, Modifier::Control);

    const auto expectedRenderer = manager.rendererConfig();
    manager.shutdown();
    auto expectedConfig = originalConfig;
    expectedConfig.initialScene = "selected-scene";
    EXPECT_EQ(Serialize(asset_json::load<AppConfig>(configFile)), Serialize(expectedConfig));
    EXPECT_EQ(Serialize(asset_json::load<RendererConfig>(rendererFile)), Serialize(expectedRenderer));

    ConfigManager reloaded;
    reloaded.init();
    EXPECT_EQ(reloaded.initialScene(), expectedConfig.initialScene);
    reloaded.shutdown();

    asset_json::save(configFile, originalConfig);
    asset_json::save(rendererFile, originalRenderer);
}

TEST(ConfigSerialization, CommandsRoundTripEnumsAndCombinedModifiers)
{
    const CommandConfig original{.bindings = {
        {.command = Command::SaveScene, .key = Key::S,
         .modifiers = Modifier::Control | Modifier::Shift, .action = Action::Press},
        {.command = Command::Navigate, .key = Key::MouseRight,
         .modifiers = Modifier::None, .action = Action::Hold},
    }};
    const json serialized = Serialize(original);
    const auto& bindings = serialized.at("bindings");
    EXPECT_EQ(bindings.at(0).at("command"), "SaveScene");
    EXPECT_EQ(bindings.at(0).at("key"), "S");
    EXPECT_EQ(bindings.at(0).at("action"), "Press");
    EXPECT_EQ(bindings.at(0).at("modifiers"), json::array({"Shift", "Control"}));
    EXPECT_EQ(bindings.at(1).at("modifiers"), json::array());

    CommandConfig restored{.bindings = {CommandBinding{}}};
    Deserialize(serialized, restored);
    ASSERT_EQ(restored.bindings.size(), original.bindings.size());
    for (size_t index = 0; index < original.bindings.size(); ++index)
    {
        EXPECT_EQ(restored.bindings[index].command, original.bindings[index].command);
        EXPECT_EQ(restored.bindings[index].key, original.bindings[index].key);
        EXPECT_EQ(restored.bindings[index].modifiers, original.bindings[index].modifiers);
        EXPECT_EQ(restored.bindings[index].action, original.bindings[index].action);
    }
}

TEST(ConfigSerialization, ReadsCommandsFileWithStaticReflection)
{
    std::ifstream input{std::filesystem::path{RENDERLAB_SOURCE_DIR} / "config" / "commands.json"};
    ASSERT_TRUE(input.is_open());
    const json stored = json::parse(input);
    CommandConfig commands;
    Deserialize(stored, commands);
    EXPECT_EQ(Serialize(commands), stored);
}

TEST(ConfigSerialization, RejectsMissingAndUnknownCommandFields)
{
    json binding = {
        {"command", "SaveScene"}, {"key", "S"},
        {"modifiers", json::array({"Control"})}, {"action", "Press"},
    };
    CommandBinding restored{};
    binding["command"] = "InvalidCommand";
    EXPECT_DEATH(Deserialize(binding, restored), "");
    binding["command"] = "SaveScene";
    binding["modifiers"] = json::array({"InvalidModifier"});
    EXPECT_DEATH(Deserialize(binding, restored), "");
    binding["modifiers"] = json::array();
    binding.erase("action");
    EXPECT_DEATH(Deserialize(binding, restored), "");
}

TEST(ConfigSerialization, RejectsInvalidContainerTypesAndFlagBits)
{
    CommandConfig restored;
    EXPECT_DEATH(Deserialize(json{{"bindings", json::object()}}, restored), "");

    Modifier modifiers{};
    EXPECT_DEATH(Deserialize(json("Control"), modifiers), "");
    EXPECT_DEATH(Deserialize(json::array({"None"}), modifiers), "");
    EXPECT_DEATH(Serialize(static_cast<Modifier>(1u << 10)), "");
    EXPECT_DEATH(Serialize(static_cast<Command>(999)), "");
}

TEST(ConfigSerialization, EnumAnnotationsDetermineSerialization)
{
    EXPECT_EQ(Serialize(OrdinaryEnum::Third), "Third");
    EXPECT_EQ(Serialize(TestFlags::Third), json::array({"Third"}));
    EXPECT_EQ(Serialize(TestFlags::All), json::array({"First", "Second", "Third"}));

    OrdinaryEnum ordinary{};
    Deserialize(json("Second"), ordinary);
    EXPECT_EQ(ordinary, OrdinaryEnum::Second);

    TestFlags flags{};
    Deserialize(json::array({"First", "Second", "Third"}), flags);
    EXPECT_EQ(flags, TestFlags::All);
}

TEST(ConfigSerialization, RoundTripsScalarsAndPaths)
{
    const auto roundTrip = []<class T>(const T& original, const json& expected)
    {
        EXPECT_EQ(Serialize(original), expected);

        T restored{};
        Deserialize(expected, restored);
        EXPECT_EQ(restored, original);
    };

    roundTrip(42, json(42));
    roundTrip(1.25, json(1.25));
    roundTrip(true, json(true));
    roundTrip(std::string{"RenderLab"}, json("RenderLab"));
    roundTrip(std::filesystem::path{"assets/models/car.gltf"}, json("assets/models/car.gltf"));
}

TEST(ConfigSerialization, RecursivelySerializesNestedVectorsAndEnums)
{
    const std::vector<std::vector<TestFlags>> original{
        {TestFlags::First, TestFlags::All}, {}, {TestFlags::None},
    };
    const json expected = json::array({
        json::array({json::array({"First"}), json::array({"First", "Second", "Third"})}),
        json::array(), json::array({json::array()}),
    });
    EXPECT_EQ(Serialize(original), expected);

    std::vector<std::vector<TestFlags>> restored;
    Deserialize(expected, restored);
    EXPECT_EQ(restored, original);

    const std::vector<OrdinaryEnum> ordinary{OrdinaryEnum::First, OrdinaryEnum::Third};
    std::vector<OrdinaryEnum> restoredOrdinary;
    Deserialize(Serialize(ordinary), restoredOrdinary);
    EXPECT_EQ(restoredOrdinary, ordinary);
}

TEST(ConfigSerialization, RecursivelySerializesVectorsOfReflectedObjects)
{
    const std::vector<AppConfig> original{sampleConfig(), sampleConfig()};
    const json serialized = Serialize(original);
    EXPECT_EQ(serialized, json::array({Serialize(sampleConfig()), Serialize(sampleConfig())}));

    std::vector<AppConfig> restored;
    Deserialize(serialized, restored);
    ASSERT_EQ(restored.size(), original.size());
    EXPECT_EQ(Serialize(restored), serialized);
    EXPECT_EQ(restored.front().initialScene, original.front().initialScene);
    EXPECT_EQ(restored.front().paths.assets, original.front().paths.assets);
}
