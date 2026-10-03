#include "asset/Serializer.h"
#include "scene/Actor.h"

#include <gtest/gtest.h>

namespace
{
struct TaggedComponent : Component
{
    void tick(float) override { ++tickCount; }

    [[=ReflectField{}]] int value = 11;

    int tickCount = 0;
    Actor* runtimeActor = nullptr;
};

struct DerivedTaggedComponent : TaggedComponent
{
    [[=ReflectField{}]] int extra = 23;
};

struct TaggedActor : Actor
{
    void tick(float deltaTime) override
    {
        ++tickCount;
        Actor::tick(deltaTime);
    }

    [[=ReflectField{}]] float health = 100.0f;

    int tickCount = 0;
    Component* runtimeComponent = nullptr;
};

struct DerivedTaggedActor : TaggedActor
{
};

static_assert(std::meta::annotations_of_with_type(^^DerivedTaggedActor, ^^PartialSerialize).empty());
static_assert(std::meta::annotations_of_with_type(^^DerivedTaggedComponent, ^^PartialSerialize).empty());
static_assert(HasInheritedAnnotation(^^DerivedTaggedActor, ^^PartialSerialize,
                                    std::meta::access_context::current()));
static_assert(HasInheritedAnnotation(^^DerivedTaggedComponent, ^^PartialSerialize,
                                    std::meta::access_context::current()));

void registerTestTypes()
{
    RegisterSceneTypes();
    static const bool registered = []
    {
        RegisterBase<DerivedTaggedActor, Actor>("PartialSerializationActor");
        RegisterBase<DerivedTaggedComponent, Component>("PartialSerializationComponent");
        return true;
    }();
}
}

TEST(PartialSerialization, ComponentIncludesOnlyMarkedInheritedMembers)
{
    TaggedActor owner;
    owner.init();
    auto& component = owner.addComponent<DerivedTaggedComponent>();
    component.value = 42;
    component.extra = 73;
    component.tickCount = 5;
    component.runtimeActor = &owner;

    EXPECT_EQ(Serialize(component), (json{{"value", 42}, {"extra", 73}}));
    EXPECT_EQ(&component.actor(), &owner);
}

TEST(PartialSerialization, DeserializationLeavesUnmarkedMembersUntouched)
{
    TaggedActor runtimeActor;
    DerivedTaggedComponent component;
    component.tickCount = 9;
    component.runtimeActor = &runtimeActor;

    const json data = {
        {"value", 42}, {"extra", 73},
        {"tickCount", 999}, {"runtimeActor", "not a persistent field"},
    };
    Deserialize(data, component);

    EXPECT_EQ(component.value, 42);
    EXPECT_EQ(component.extra, 73);
    EXPECT_EQ(component.tickCount, 9);
    EXPECT_EQ(component.runtimeActor, &runtimeActor);
    EXPECT_EQ(Serialize(component), (json{{"value", 42}, {"extra", 73}}));
}

TEST(PartialSerialization, ActorPolicyIsInheritedAndPreservesRuntimeMembers)
{
    registerTestTypes();
    TaggedActor runtimeActor;
    DerivedTaggedActor source;
    source.name = "Tagged actor";
    source.health = 37.0f;
    source.tickCount = 5;
    source.runtimeComponent = runtimeActor.getComponent<TransformComponent>();

    const json stored = Serialize(source);
    EXPECT_EQ(stored.size(), 3);
    EXPECT_EQ(stored.at("name"), source.name);
    EXPECT_EQ(stored.at("health"), source.health);
    ASSERT_EQ(stored.at("components").size(), 1);
    EXPECT_EQ(stored.at("components")[0].size(), 1);
    EXPECT_TRUE(stored.at("components")[0].contains("TransformComponent"));
    EXPECT_FALSE(stored.contains("tickCount"));
    EXPECT_FALSE(stored.contains("runtimeComponent"));

    DerivedTaggedActor restored;
    restored.tickCount = 9;
    restored.runtimeComponent = source.runtimeComponent;
    Deserialize(stored, restored);
    restored.init();

    EXPECT_EQ(restored.tickCount, 9);
    EXPECT_EQ(restored.runtimeComponent, source.runtimeComponent);
    EXPECT_EQ(&restored.transform().actor(), &restored);
    EXPECT_EQ(Serialize(restored), stored);
}

TEST(PartialSerialization, OwnedPolymorphicPointersStillRoundTrip)
{
    registerTestTypes();
    std::unique_ptr<Actor> source = std::make_unique<DerivedTaggedActor>();
    auto& actor = static_cast<DerivedTaggedActor&>(*source);
    actor.name = "Polymorphic actor";
    actor.health = 37.0f;
    actor.tickCount = 5;
    auto& component = actor.addComponent<DerivedTaggedComponent>();
    component.value = 42;
    component.extra = 73;
    component.tickCount = 7;
    component.runtimeActor = &actor;
    actor.runtimeComponent = &component;

    const json stored = Serialize(source);
    ASSERT_EQ(stored.size(), 1);
    const auto& storedComponent = stored.at("PartialSerializationActor").at("components")[1];
    ASSERT_EQ(storedComponent.size(), 1);
    EXPECT_EQ(storedComponent.at("PartialSerializationComponent"), (json{{"value", 42}, {"extra", 73}}));

    std::unique_ptr<Actor> restored;
    Deserialize(stored, restored);
    restored->init();
    auto* restoredActor = dynamic_cast<DerivedTaggedActor*>(restored.get());
    ASSERT_NE(restoredActor, nullptr);
    auto* restoredComponent = restoredActor->getComponent<DerivedTaggedComponent>();
    ASSERT_NE(restoredComponent, nullptr);

    EXPECT_EQ(restoredActor->tickCount, 0);
    EXPECT_EQ(restoredActor->runtimeComponent, nullptr);
    EXPECT_EQ(restoredComponent->tickCount, 0);
    EXPECT_EQ(restoredComponent->runtimeActor, nullptr);
    EXPECT_EQ(&restoredComponent->actor(), restoredActor);
    EXPECT_EQ(Serialize(restored), stored);
}

TEST(PartialSerialization, MissingMarkedFieldsRemainErrors)
{
    DerivedTaggedComponent component;
    EXPECT_DEATH(Deserialize(json{{"value", 42}}, component), "");
    EXPECT_DEATH(Deserialize(json{{"extra", 73}}, component), "");
}

TEST(PartialSerialization, RuntimeMemberIterationUsesTheSameMarkedFields)
{
    registerTestTypes();
    DerivedTaggedComponent component;
    std::vector<std::string_view> members;
    BaseTypeInfo<Component>::baseTypeInfoMap.at("PartialSerializationComponent").IterateMembers(
        component, [&](MemberRef member) { members.push_back(member.name); });
    EXPECT_EQ(members, (std::vector<std::string_view>{"value", "extra"}));
}
