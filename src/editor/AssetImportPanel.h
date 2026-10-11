#pragma once
#include "editor/EnvironmentImportTask.h"
#include "editor/MeshImportPreview.h"
class AssetImportPanel
{
public:
    void draw();
    void shutdown() noexcept;

private:
    int                         importAssetType = 0;
    ImportTextureSetting        textureSetting;
    ImportEnvironmentMapSetting environmentSetting;
    ImportMeshSetting           meshSetting;
    MeshImportPreview           meshPreview;
    std::string                 importStatus;

    EnvironmentImportTask environmentImport;
    void                  drawDialog();
};
