#include "scene/Component.h"

#include "core/Logger.h"

void Component::init(AActor* actor) { owner = actor; }

AActor& Component::actor() const { return *owner; }
