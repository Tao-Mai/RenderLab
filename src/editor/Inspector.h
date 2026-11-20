#pragma once
class AActor;
class AssetManager;
struct EditorCameraConfig;
struct ExposureConfig;
struct SceneAsset;

bool drawCameraInspector(EditorCameraConfig& camera, const ExposureConfig& globalExposure);
bool drawEnvironmentInspector(SceneAsset& scene, const AssetManager& assets);
bool drawObjectInspector(AActor* object);
