#pragma once
class AActor;
struct EditorCameraConfig;
struct ExposureConfig;

bool drawExposureSettings(ExposureConfig& exposure);
bool drawCameraInspector(EditorCameraConfig& camera, const ExposureConfig& globalExposure);
bool drawObjectInspector(AActor* object);
