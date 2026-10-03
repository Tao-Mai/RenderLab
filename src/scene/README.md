# Actor / Component

SceneManager owns a live `SceneAsset` and its `unique_ptr<Actor>` objects. Each Actor owns its
`unique_ptr<Component>` objects, including exactly one TransformComponent.
Components hold a non-owning, private Actor pointer. Runtime pointers and state
are not serialized.

Engine ticks SceneManager. The active FreeFlyCameraActor ticks first; inside that
actor, CameraComponent updates navigation and view direction before movement.
Other actors then tick their components. Actor and Component are abstract bases
with pure virtual `void tick(float deltaTime)`. Each concrete type implements Tick;
components without per-frame behavior implement an empty Tick. Editor edits mark
the scene dirty independently of Tick.

FreeFlyCameraActor supplies CameraComponent and FreeFlyMoveComponent.
StaticMeshActor supplies TransformComponent and RenderComponent.
ALight supplies TransformComponent and LightComponent. Concrete actors can
hold additional behavior components; Actor itself is not registered or instantiated.

## Registration and persistence

`RegisterSceneTypes()` in `core/Reflect.cpp` registers the concrete Actor and
Component types once during application startup. Add new concrete subclasses
to the shared `scene/SceneTypes.h` list. Its explicit names are the persistent
type IDs; serialization and editor drawing use the same list.

Polymorphic pointers serialize as `{"TypeName": {...}}`. Each non-null pointer
has exactly one registered type-name key with an object value; null pointers
serialize as `null`. Unknown types and malformed wrappers are errors.
RegisterBase generates construction, static-reflection serialization and
member-iteration callbacks; subclasses do not need serialization overrides.
Actor and Component carry `[[=PartialSerialize{}]]`. The serializer checks base
classes recursively, so subclasses inherit this opt-in policy without another
class annotation. Only accessible members marked `[[=ReflectField{}]]` are
serialized and deserialized, including marked inherited members. Unmarked
members retain their current or constructor-initialized values on deserialization.
The owned `components` field is marked; its polymorphic pointers still serialize
as objects rather than addresses. Owner pointers, picking IDs and navigation
state are unmarked. Unknown types and missing marked fields are errors.

Ordinary configuration and asset description structs still serialize all
accessible fields. `core/Reflect.h` supplies the same inherited field selection
to serialization, member iteration, and generic editor drawing.

Scene JSON uses the member names `id`, `actors`, `environment`, `name` and
`components`. Asset IDs use `{"value": "..."}`, GLM vectors use numeric arrays,
and quaternions use [x, y, z, w]. Optional material fields are explicitly null
when absent. There is no old scene-format parser.

AssetManager caches a `SceneAsset` snapshot. SceneManager clones it through Serialize/Deserialize before
binding component owners, validating required components, and assigning runtime
picking IDs. Saving creates a detached snapshot again. The asset cache never
owns references into the live scene. Picking IDs are not persisted and are not
reused after reload.

Renderer/GpuScene and render passes hold Actor references and retrieve the
components they need; they do not use an entity registry. Editor gizmos and
picking use the same actors. The inspector draws marked fields with typed ImGui
overloads. Transform, Light and Render have dedicated editors for Euler rotation,
light-type-specific properties and submesh material overrides. Mesh selection
notifies Renderer to refresh GPU scene resources; ordinary edits mark the scene
dirty without rebuilding the scene. UI callbacks and ImGui dependencies stay in
the editor layer.
