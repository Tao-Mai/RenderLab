#include "editor/ComponentDraw.h"

#include "asset/ApplyOptionalFields.h"
#include "asset/AssetManager.h"
#include "core/Context.h"
#include "scene/SceneTypes.h"

#include <limits>

#include <gtest/gtest.h>
#include <imgui_internal.h>

namespace
{
enum class TestEnum { First = 7, Second = 42, Third = 99 };
enum class [[=Flags{}]] TestFlags { None = 0, First = 1, Second = 2 };

struct DrawTestComponent : Component
{
    void tick(float) override {}

    [[=ReflectField{}]] bool enabled = false;
    [[=ReflectField{}]] double precise = 1.234567890123;
    [[=ReflectField{}]] uint64_t large = std::numeric_limits<uint64_t>::max() - 4;
    [[=ReflectField{}]] std::string text = std::string(512, 'x');

    int transient = 9;
    AActor* ownerPointer = nullptr;
};

struct DerivedDrawTestComponent : DrawTestComponent
{
    [[=ReflectField{}]] glm::vec3 offset{1.0f, 2.0f, 3.0f};
};

class ComponentDrawTest : public testing::Test
{
protected:
    Context previousContext;

    void SetUp() override
    {
        previousContext = context();
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1200.0f, 1600.0f};
        io.DeltaTime = 1.0f / 60.0f;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ASSERT_TRUE(io.Fonts->Build());
    }

    void TearDown() override
    {
        ImGui::DestroyContext();
        context() = previousContext;
    }

    template<class F>
    std::string frame(F&& draw)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize({1100.0f, 1550.0f});
        ImGui::Begin("Inspector test", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::LogToBuffer(64);
        draw();
        const std::string text = GImGui->LogBuffer.c_str();
        ImGui::LogFinish();
        ImGui::End();
        ImGui::Render();
        return text;
    }

    template<class F, class Id>
    ComponentDrawResult enterScalar(F&& draw, Id&& getId, const char* text)
    {
        ImGuiID input = 0;
        frame([&]
        {
            input = getId();
            GImGui->NavActivateId = input;
            GImGui->NavActivateFlags = ImGuiActivateFlags_PreferInput;
            (void)draw();
        });
        EXPECT_EQ(GImGui->TempInputId, input);

        ImGui::GetIO().AddInputCharactersUTF8(text);
        ComponentDrawResult result;
        frame([&] { result = draw(); });
        ImGui::ClearActiveID();
        return result;
    }

    static ImGuiID vectorElementId(const char* label, int index)
    {
        ImGui::PushID(label);
        ImGui::PushID(index);
        const auto id = ImGui::GetID("");
        ImGui::PopID();
        ImGui::PopID();
        return id;
    }
};
}

TEST_F(ComponentDrawTest, GenericDrawingUsesMarkedInheritedFieldsWithoutChangingValues)
{
    AStaticMesh owner;
    DerivedDrawTestComponent component;
    component.ownerPointer = &owner;
    ComponentDrawResult result;
    const auto text = frame([&] { result = component_draw::drawFields(component); });

    EXPECT_FALSE(result.edited);
    EXPECT_NE(text.find("enabled"), std::string::npos);
    EXPECT_NE(text.find("offset"), std::string::npos);
    EXPECT_EQ(text.find("transient"), std::string::npos);
    EXPECT_EQ(text.find("ownerPointer"), std::string::npos);
    EXPECT_DOUBLE_EQ(component.precise, 1.234567890123);
    EXPECT_EQ(component.large, std::numeric_limits<uint64_t>::max() - 4);
    EXPECT_EQ(component.text, std::string(512, 'x'));
    EXPECT_EQ(component.ownerPointer, &owner);
}

TEST_F(ComponentDrawTest, GenericCheckboxEditsTheOriginalMember)
{
    DerivedDrawTestComponent component;
    ImGuiID checkbox = 0;
    frame([&]
    {
        ImGui::PushID("enabled");
        checkbox = ImGui::GetID("enabled");
        ImGui::PopID();
        (void)component_draw::drawFields(component);
    });

    ImGui::ActivateItemByID(checkbox);
    ComponentDrawResult result;
    frame([&] { result = component_draw::drawFields(component); });
    EXPECT_TRUE(result.edited);
    EXPECT_TRUE(component.enabled);
}

TEST_F(ComponentDrawTest, ReflectedEnumComboUsesActualEnumeratorValues)
{
    TestEnum value = TestEnum::First;
    ImGuiID combo = 0;
    frame([&]
    {
        combo = ImGui::GetID("Enum");
        (void)component_draw::drawField("Enum", value);
    });

    ImGui::ActivateItemByID(combo);
    frame([&] { (void)component_draw::drawField("Enum", value); });
    auto* popup = ImGui::FindWindowByName("##Combo_00");
    ASSERT_NE(popup, nullptr);
    ASSERT_TRUE(popup->Active);
    ImGui::ActivateItemByID(popup->GetID("Third"));
    bool edited = false;
    frame([&] { edited = component_draw::drawField("Enum", value); });

    EXPECT_TRUE(edited);
    EXPECT_EQ(value, TestEnum::Third);
}

TEST_F(ComponentDrawTest, GenericDrawingSupportsFlagsOptionalContainersAndNestedObjects)
{
    TestFlags flags = TestFlags::First;
    std::optional<float> optional = 0.5f;
    std::vector<std::vector<float>> vectors{{1.0f, 2.0f}};
    std::unordered_map<int, std::optional<float>> values{{3, 0.25f}};
    TextureBinding binding{TextureAsset::ID{"white"}, SamplerAsset::ID{"linearRepeat"}};

    frame([&]
    {
        EXPECT_FALSE(component_draw::drawField("Flags", flags));
        EXPECT_FALSE(component_draw::drawField("Optional", optional));
        EXPECT_FALSE(component_draw::drawField("Vectors", vectors));
        EXPECT_FALSE(component_draw::drawField("Map", values));
        EXPECT_FALSE(component_draw::drawField("Binding", binding));
    });
    EXPECT_EQ(flags, TestFlags::First);
    EXPECT_EQ(optional, 0.5f);
    EXPECT_EQ(values.at(3), 0.25f);
    EXPECT_EQ(binding.textureID, TextureAsset::ID{"white"});
}

TEST_F(ComponentDrawTest, LightEditorShowsOnlyTypeSpecificFields)
{
    forEachLightComponentType([&]<class T>(const char*)
    {
        T light;
        ComponentDrawResult result;
        const auto text = frame([&] { result = drawComponent(light); });
        EXPECT_FALSE(result.edited);
        const auto shown = [&](const char* label) { return text.find(label) != std::string::npos; };
        EXPECT_EQ(shown("Illuminance (lux)"), (std::same_as<T, DirectionalLightComponent>));
        EXPECT_EQ(shown("Luminous intensity (cd)"), (std::derived_from<T, LocalLightComponent>));
        EXPECT_TRUE(shown("Cast Shadow"));
        EXPECT_EQ(shown("Range"), (std::derived_from<T, LocalLightComponent>));
        EXPECT_EQ(shown("Inner Angle"), (std::same_as<T, SpotLightComponent>));
        EXPECT_EQ(shown("Outer Angle"), (std::same_as<T, SpotLightComponent>));
        EXPECT_EQ(shown("Area Size"), (std::same_as<T, RectAreaLightComponent>));
    });
}

TEST_F(ComponentDrawTest, LightTypeSelectionReplacesComponentAndPreservesCommonProperties)
{
    ALight actor;
    actor.init();
    auto* point = actor.getComponent<PointLightComponent>();
    point->color = {0.2f, 0.4f, 0.6f};
    point->intensity = 42.0f;
    point->range = 25.0f;
    point->castShadow = true;
    const auto draw = [&] { return drawComponent(*actor.getComponent<LightComponent>()); };
    ImGuiID combo = 0;
    frame([&]
    {
        ImGui::PushID(point);
        combo = ImGui::GetID("Type");
        ImGui::PopID();
        (void)draw();
    });
    ImGui::ActivateItemByID(combo);
    frame([&] { (void)draw(); });
    auto* popup = ImGui::FindWindowByName("##Combo_00");
    ASSERT_NE(popup, nullptr);
    ASSERT_TRUE(popup->Active);
    ImGui::ActivateItemByID(popup->GetID("RectArea"));
    ComponentDrawResult result;
    frame([&] { result = draw(); });
    EXPECT_TRUE(result.edited);
    auto* area = actor.getComponent<RectAreaLightComponent>();
    ASSERT_NE(area, nullptr);
    EXPECT_EQ(actor.getComponent<PointLightComponent>(), nullptr);
    EXPECT_EQ(actor.components.size(), 2u);
    EXPECT_EQ(&area->actor(), &actor);
    EXPECT_EQ(area->color, glm::vec3(0.2f, 0.4f, 0.6f));
    EXPECT_FLOAT_EQ(area->intensity, 42.0f);
    EXPECT_FLOAT_EQ(area->range, 25.0f);
    EXPECT_TRUE(area->castShadow);
}

TEST_F(ComponentDrawTest, TransformUsesEulerAnglesAndDoesNotModifyIdleQuaternion)
{
    TransformComponent transform;
    transform.setRotationEulerDegrees({20.0f, -30.0f, 45.0f});
    const auto rotation = transform.rotation;
    const auto text = frame([&] { EXPECT_FALSE(drawComponent(transform).edited); });
    EXPECT_NE(text.find("deg"), std::string::npos);
    EXPECT_EQ(transform.rotation, rotation);
}

TEST_F(ComponentDrawTest, TransformEditsEulerRotationAndClampsScale)
{
    TransformComponent transform;
    const auto draw = [&] { return component_draw::drawFields(transform); };
    const auto rotation = enterScalar(draw, [] { return vectorElementId("Rotation", 0); }, "60");
    EXPECT_TRUE(rotation.edited);
    EXPECT_NEAR(transform.rotationEulerDegrees().x, 60.0f, 0.001f);
    EXPECT_NEAR(glm::length(transform.rotation), 1.0f, 0.0001f);

    const auto scale = enterScalar(draw, [] { return vectorElementId("Scale", 0); }, "-5");
    EXPECT_TRUE(scale.edited);
    EXPECT_FLOAT_EQ(transform.scale.x, 0.001f);
}

TEST_F(ComponentDrawTest, RotationRetainsEnteredAnglesAcrossEulerBoundaries)
{
    TransformComponent transform;
    const auto draw = [&] { return component_draw::drawFields(transform); };
    EXPECT_TRUE(enterScalar(draw, [] { return vectorElementId("Rotation", 1); }, "100").edited);
    auto text = frame([&] { EXPECT_FALSE(draw().edited); });
    EXPECT_NE(text.find("100.0 deg"), std::string::npos);
    EXPECT_NEAR(std::abs(glm::dot(transform.rotation, glm::quat(glm::radians(glm::vec3{0.0f, 100.0f, 0.0f})))), 1.0f, 1e-6f);

    EXPECT_TRUE(enterScalar(draw, [] { return vectorElementId("Rotation", 1); }, "190").edited);
    text = frame([&] { EXPECT_FALSE(draw().edited); });
    EXPECT_NE(text.find("190.0 deg"), std::string::npos);

    // Gizmo/runtime edits must refresh the cached inspector representation.
    transform.setRotationEulerDegrees({10.0f, 20.0f, 30.0f});
    text = frame([&] { EXPECT_FALSE(draw().edited); });
    EXPECT_NE(text.find("20.0 deg"), std::string::npos);
    EXPECT_EQ(text.find("190.0 deg"), std::string::npos);
}

TEST_F(ComponentDrawTest, LightAngleInputIsClampedAndStoredAsCosine)
{
    SpotLightComponent light;
    const float outer  = light.cosOuter;
    const auto  result = enterScalar(
        [&] { return component_draw::drawFields(light); },
        [] { return ImGui::GetID("Inner Angle"); },
        "100"
    );
    EXPECT_TRUE(result.edited);
    EXPECT_NEAR(light.cosInner, outer, 0.0001f);
    EXPECT_FLOAT_EQ(light.cosOuter, outer);
}

TEST_F(ComponentDrawTest, EveryRegisteredComponentHasAnInspector)
{
    AssetManager assets;
    context().assetManager = &assets;
    forEachComponentType(
        [&]<class T>(const char* name)
        {
            T component;
            if constexpr (std::same_as<T, RenderComponent>)
                component.meshId = BuiltinAssets::Mesh::sphere;
            const auto text = frame([&] { EXPECT_FALSE(drawComponent(component).edited); });
            EXPECT_NE(text.find(name), std::string::npos);
            EXPECT_EQ(text.find("no registered inspector"), std::string::npos);
        }
    );
}

TEST_F(ComponentDrawTest, MeshSelectionClearsMaterialOverrides)
{
    AssetManager assets;
    context().assetManager = &assets;
    RenderComponent render;
    render.meshId = BuiltinAssets::Mesh::sphere;
    render.materialOverrides[0].baseColorFactor = glm::vec4{0.2f, 0.8f, 0.3f, 1.0f};
    render.materialOverrides[9].roughness = 0.3f;
    ImGuiID combo = 0;
    frame([&]
    {
        combo = ImGui::GetID("Mesh");
        (void)component_draw::drawFields(render);
    });

    ImGui::ActivateItemByID(combo);
    frame([&] { (void)component_draw::drawFields(render); });
    EXPECT_EQ(render.materialOverrides.size(), 2);
    auto* popup = ImGui::FindWindowByName("##Combo_00");
    ASSERT_NE(popup, nullptr);
    ASSERT_TRUE(popup->Active);
    ImGui::ActivateItemByID(popup->GetID("cube"));
    ComponentDrawResult result;
    frame([&] { result = component_draw::drawFields(render); });

    EXPECT_EQ(render.meshId, BuiltinAssets::Mesh::cube);
    EXPECT_TRUE(result.edited);
    EXPECT_TRUE(render.materialOverrides.empty());
}

TEST_F(ComponentDrawTest, ResetOverridesOnlyChangesTheChosenSubmesh)
{
    AssetManager assets;
    context().assetManager = &assets;
    RenderComponent render;
    render.meshId = BuiltinAssets::Mesh::sphere;
    render.materialOverrides[0].roughness = 0.3f;
    render.materialOverrides[9].metallic = 0.8f;
    ImGuiID reset = 0;
    frame([&]
    {
        ImGui::PushID(0);
        ImGui::PushID("Material");
        reset = ImGui::GetID("Reset overrides");
        ImGui::PopID();
        ImGui::PopID();
        (void)component_draw::drawFields(render);
    });

    ImGui::ActivateItemByID(reset);
    ComponentDrawResult result;
    frame([&] { result = component_draw::drawFields(render); });
    EXPECT_TRUE(result.edited);
    EXPECT_FALSE(render.materialOverrides.contains(0));
    EXPECT_EQ(render.materialOverrides.at(9).metallic, 0.8f);
}

TEST(ComponentMaterialEdit, DiffRetainsEveryOptionalOverrideIncludingTexturesAndShader)
{
    PbrMaterialAsset stock;
    stock.id = MaterialAsset::ID{"white"};
    stock.roughness = 0.8f;
    stock.baseColorTexture = TextureBinding{TextureAsset::ID{"white"}, SamplerAsset::ID{"linearRepeat"}};
    PbrMaterialAsset preview = stock;
    preview.roughness = 0.3f;
    preview.shaderId = ShaderAsset::ID{"custom"};
    preview.baseColorTexture = TextureBinding{TextureAsset::ID{"albedo"}, SamplerAsset::ID{"linearClamp"}};
    preview.normalTexture = TextureBinding{TextureAsset::ID{"normal"}, SamplerAsset::ID{"linearRepeat"}};
    preview.emissive = glm::vec3{0.1f, 0.2f, 0.3f};
    preview.doubleSided = true;

    const auto patch = component_draw::materialOverrideDiff(stock, preview);
    EXPECT_TRUE(component_draw::hasMaterialOverrides(patch));
    EXPECT_TRUE(patch.id.empty());
    EXPECT_EQ(patch.roughness, preview.roughness);
    EXPECT_EQ(patch.shaderId, preview.shaderId);
    EXPECT_EQ(patch.baseColorTexture, preview.baseColorTexture);
    EXPECT_EQ(patch.normalTexture, preview.normalTexture);
    EXPECT_EQ(patch.emissive, preview.emissive);
    EXPECT_EQ(patch.doubleSided, preview.doubleSided);

    auto merged = stock;
    applyOptionalFields(merged, patch);
    EXPECT_EQ(component_draw::materialOverrideDiff(merged, preview).roughness, std::nullopt);
    EXPECT_FALSE(component_draw::hasMaterialOverrides(component_draw::materialOverrideDiff(merged, preview)));
    EXPECT_FALSE(component_draw::hasMaterialOverrides(component_draw::materialOverrideDiff(stock, stock)));
}

TEST(ComponentMaterialEdit, MergeIgnoresNonOptionalAndEmptyFields)
{
    PbrMaterialAsset stock;
    stock.id = MaterialAsset::ID{"stock"};
    stock.roughness = 0.8f;
    stock.doubleSided = true;
    stock.emissive = glm::vec3{0.1f};
    stock.baseColorTexture = TextureBinding{TextureAsset::ID{"white"}, SamplerAsset::ID{"linearRepeat"}};

    MaterialOverride patch;
    patch.id = MaterialAsset::ID{"not-an-override"};
    patch.roughness = 0.0f;
    patch.doubleSided = false;
    applyOptionalFields(stock, patch);

    EXPECT_EQ(stock.id, MaterialAsset::ID{"stock"});
    EXPECT_EQ(stock.roughness, 0.0f);
    EXPECT_EQ(stock.doubleSided, false);
    EXPECT_EQ(stock.emissive, glm::vec3{0.1f});
    ASSERT_TRUE(stock.baseColorTexture);
    EXPECT_EQ(stock.baseColorTexture->textureID, TextureAsset::ID{"white"});
}
