#pragma once
class AActor;
class AssetManager;
struct EditorCameraConfig;
struct SceneAsset;

bool drawCameraInspector(EditorCameraConfig& camera);
bool drawEnvironmentInspector(SceneAsset& scene, const AssetManager& assets);
bool drawObjectInspector(AActor* object);
