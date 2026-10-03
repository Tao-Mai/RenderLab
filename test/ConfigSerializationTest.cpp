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
        .paths = {.assets = "../assets", .commands = "commands.json"},
        .initialScene = SceneAsset::ID{"default"},
        .renderer = {.brdfLut = {TextureAsset::ID{"lut_ggx"}, SamplerAsset::ID{"linearClamp"}}},
    };
}
}

TEST(ConfigSerialization, UsesMemberNamesAndReflectedAssetIds)
{
    const json serialized = Serialize(sampleConfig());

    EXPECT_EQ(serialized.at("paths").at("assets"), "../assets");
    EXPECT_EQ(serialized.at("paths").at("commands"), "commands.json");
    EXPECT_EQ(serialized.at("initialScene").at("value"), "default");
    EXPECT_EQ(serialized.at("renderer").at("brdfLut").at("textureID").at("value"), "lut_ggx");
    EXPECT_EQ(serialized.at("renderer").at("brdfLut").at("samplerID").at("value"), "linearClamp");
}

TEST(ConfigSerialization, RoundTripsNestedConfig)
{
    const AppConfig original = sampleConfig();
    AppConfig restored;
    Deserialize(json::parse(Serialize(original).dump()), restored);

    EXPECT_EQ(restored.paths.assets, original.paths.assets);
    EXPECT_EQ(restored.paths.commands, original.paths.commands);
    EXPECT_EQ(restored.initialScene, original.initialScene);
    EXPECT_EQ(restored.renderer.brdfLut.textureID, original.renderer.brdfLut.textureID);
    EXPECT_EQ(restored.renderer.brdfLut.samplerID, original.renderer.brdfLut.samplerID);
}

TEST(ConfigSerialization, MissingFieldsFailInsteadOfUsingDefaults)
{
    json serialized = Serialize(sampleConfig());
    serialized.at("paths").erase("commands");
    AppConfig restored;
    EXPECT_DEATH(Deserialize(serialized, restored), "");

    serialized = Serialize(sampleConfig());
    serialized.at("renderer").at("brdfLut").at("samplerID").erase("value");
    EXPECT_DEATH(Deserialize(serialized, restored), "");
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
    ConfigManager manager;
    manager.init();

    const auto source = std::filesystem::path{RENDERLAB_SOURCE_DIR};
    EXPECT_EQ(std::filesystem::weakly_canonical(manager.paths().assets), source / "test");
    EXPECT_EQ(manager.paths().commands, source / "config" / "commands.json");
    EXPECT_EQ(manager.initialScene(), SceneAsset::ID{"default"});
    EXPECT_EQ(manager.commandConfig().bindings.size(), 16);
    EXPECT_EQ(manager.commandConfig().bindings.front().command, Command::Quit);
    EXPECT_EQ(manager.commandConfig().bindings.at(12).command, Command::SaveScene);
    EXPECT_EQ(manager.commandConfig().bindings.at(12).modifiers, Modifier::Control);

    manager.shutdown();
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
