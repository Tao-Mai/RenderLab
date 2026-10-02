# Actor / Component

SceneManager owns the live scene's `unique_ptr<Actor>` objects. Each Actor owns its
`unique_ptr<Component>` objects, including exactly one TransformComponent.
Components hold a non-owning, private Actor pointer. Runtime pointers and state
are not serialized.

Engine ticks SceneManager. The active FreeFlyCameraActor ticks first; inside that
actor, CameraComponent updates navigation and view direction before movement.
Other actors then tick their components. Tick returns whether persistent scene
data changed, so the editor can mark the scene dirty.

FreeFlyCameraActor supplies CameraComponent and FreeFlyMoveComponent.
StaticMeshActor supplies TransformComponent and RenderComponent.
An ordinary Actor can hold LightComponent or additional behavior components.

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

SceneManager clones the asset snapshot through Serialize/Deserialize before
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
