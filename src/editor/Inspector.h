#pragma once
class AActor;
class AssetManager;
struct EditorCameraConfig;
class Renderer;
struct SceneAsset;

bool drawCameraInspector(EditorCameraConfig& camera, const Renderer& renderer);
bool drawEnvironmentInspector(SceneAsset& scene, const AssetManager& assets);
bool drawObjectInspector(AActor* object);
