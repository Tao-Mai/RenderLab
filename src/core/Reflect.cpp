#include "core/Reflect.h"

#include "asset/Serializer.h"
#include "scene/SceneTypes.h"

void RegisterSceneTypes()
{
    static const bool registered = []
    {
        forEachComponentType([]<class T>(std::string_view name) { RegisterBase<T, Component>(name); });
        forEachActorType([]<class T>(std::string_view name) { RegisterBase<T, AActor>(name); });
        return true;
    }();
}
