#include "editor/AssetImportPanel.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <imgui.h>
#include <imgui_stdlib.h>
#include "asset/import/ImportId.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <commdlg.h>
namespace
{
[[nodiscard]] std::string displayPath(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

void drawSourcePath(std::filesystem::path& source, const wchar_t* filter)
{
    if (ImGui::Button("Source file..."))
    {
        std::array<wchar_t, 32768> selectedFile{};
        const std::wstring         initialDirectory = context().config->paths().assets.parent_path().wstring();

        OPENFILENAMEW dialog{};
        dialog.lStructSize     = sizeof(dialog);
        dialog.hwndOwner       = GetActiveWindow();
        dialog.lpstrFilter     = filter;
        dialog.lpstrFile       = selectedFile.data();
        dialog.nMaxFile        = static_cast<DWORD>(selectedFile.size());
        dialog.lpstrInitialDir = initialDirectory.c_str();
        dialog.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;

        if (GetOpenFileNameW(&dialog))
        {
            source = selectedFile.data();
        }
    }

    if (!source.empty())
    {
        const std::string label = displayPath(source);
        ImGui::TextWrapped("%s", label.c_str());
    }
}

[[nodiscard]] bool sourceIsImportable(const std::filesystem::path& source, int assetType)
{
    std::error_code error;
    if (source.empty() || !std::filesystem::is_regular_file(source, error))
    {
        return false;
    }

    std::string extension = source.extension().string();
    std::ranges::transform(
        extension,
        extension.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); }
    );
    if (assetType == 2 || assetType == 3)
    {
        return AssetImporter::supportsModel(source);
    }
    return extension == ".exr" || extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
        extension == ".hdr" || extension == ".bmp" || extension == ".tga" || extension == ".gif" ||
        extension == ".psd" || extension == ".pic" || extension == ".pnm";
}

void drawColorSpace(std::optional<ColorSpace>& colorSpace)
{
    int selected = 0;
    if (colorSpace)
    {
        selected = *colorSpace == ColorSpace::Linear ? 1 : 2;
    }
    if (ImGui::Combo("Color space", &selected, "Auto\0Linear\0sRGB\0"))
    {
        colorSpace =
            selected == 0 ? std::nullopt : std::optional{selected == 1 ? ColorSpace::Linear : ColorSpace::Srgb};
    }
}

void drawPositiveUint(const char* label, uint32_t& value)
{
    constexpr uint32_t step = 1;
    ImGui::InputScalar(label, ImGuiDataType_U32, &value, &step);
}
}  // namespace
void AssetImportPanel::draw()
{
    if (auto status = environmentImport.poll())
        importStatus = std::move(*status);
    ImGui::SeparatorText("Assets");
    constexpr const char* assetTypes[] = {"Texture", "Environment map", "Mesh", "Scene"};
    ImGui::BeginDisabled(environmentImport.busy());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##Import asset type", &importAssetType, assetTypes, static_cast<int>(std::size(assetTypes)));
    if (ImGui::Button("Import"))
    {
        textureSetting     = {};
        environmentSetting = {};
        meshSetting        = {};
        sceneSetting       = {};
        meshPreview.clear();
        ImGui::OpenPopup("Import Asset");
    }
    ImGui::EndDisabled();
    if (environmentImport.busy())
    {
        ImGui::SameLine();
        if (ImGui::Button("Cancel import"))
        {
            environmentImport.cancel();
            importStatus = "Canceling environment import...";
        }
    }
    if (!importStatus.empty())
    {
        ImGui::TextWrapped("%s", importStatus.c_str());
    }
    drawDialog();
}
void AssetImportPanel::drawDialog()
{
    if (!ImGui::BeginPopupModal("Import Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    static constexpr wchar_t imageFilter[] =
        L"Image files (*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.psd;*.pic;*.pnm;*.exr;*.hdr)\0"
        L"*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.psd;*.pic;*.pnm;*.exr;*.hdr\0"
        L"All files (*.*)\0*.*\0";
    static const std::wstring kModelFilter = []
    {
        const auto extensions = AssetImporter::modelExtensions();
        std::wstring filter = L"Model files";
        filter += L'\0';
        filter.append(extensions.begin(), extensions.end());
        filter += L'\0';
        filter += L"All files (*.*)";
        filter += L'\0';
        filter += L"*.*";
        filter.append(2, L'\0');
        return filter;
    }();

    std::filesystem::path* source = nullptr;
    switch (importAssetType)
    {
        case 0:
            source = &textureSetting.source;
            drawSourcePath(*source, imageFilter);
            drawColorSpace(textureSetting.colorSpace);
            break;
        case 1:
            source = &environmentSetting.source;
            drawSourcePath(*source, imageFilter);
            drawColorSpace(environmentSetting.colorSpace);
            drawPositiveUint("Irradiance face size", environmentSetting.irradianceSize);
            drawPositiveUint("Irradiance samples", environmentSetting.irradianceSampleCount);
            drawPositiveUint("Prefilter first mip samples", environmentSetting.prefilteredSpecularSampleCount);
            drawPositiveUint("Prefilter sample limit", environmentSetting.prefilteredSpecularMaxSampleCount);
            break;
        case 2:
            {
                source                    = &meshSetting.source;
                const auto previousSource = *source;
                drawSourcePath(*source, kModelFilter.c_str());
                if (*source != previousSource)
                {
                    meshPreview.clear();
                    meshSetting.id = source->stem().string();
                }
                if (sourceIsImportable(*source, importAssetType) && !meshPreview.ready())
                    meshPreview.load(*source);

                ImGui::InputText("Mesh ID", &meshSetting.id.value);
                ImGui::InputFloat("Import scale", &meshSetting.importScale, 0.1f, 1.0f, "%.6g");
                if (meshPreview.ready())
                {
                    const auto& prepared    = meshPreview.prepared();
                    const auto  size        = prepared.boundsMax - prepared.boundsMin;
                    const float longestSide = std::max({size.x, size.y, size.z});
                    float       targetSize  = longestSide * meshSetting.importScale;
                    if (ImGui::InputFloat("Target longest side (m)", &targetSize, 0.1f, 1.0f, "%.6g") &&
                        std::isfinite(targetSize) && targetSize > 0.0f && longestSide > 0.0f)
                        meshSetting.importScale = targetSize / longestSide;
                }
                meshPreview.draw(meshSetting.importScale);
                break;
            }
        case 3:
            {
                source = &sceneSetting.source;
                const auto previousSource = *source;
                drawSourcePath(*source, kModelFilter.c_str());
                if (*source != previousSource)
                    sceneSetting.id = source->stem().string();
                ImGui::InputText("Scene ID", &sceneSetting.id.value);
                ImGui::InputFloat("Import scale", &sceneSetting.importScale, 0.1f, 1.0f, "%.6g");
                ImGui::TextUnformatted("Keep mesh instances; import or fit an editor camera.");
                break;
            }
        default:
            DCHECK(false, "invalid import asset type");
    }

    const bool  validSource     = sourceIsImportable(*source, importAssetType);
    std::string sourceExtension = source->extension().string();
    std::ranges::transform(
        sourceExtension,
        sourceExtension.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); }
    );
    const bool validColorSpace = (sourceExtension != ".exr" && sourceExtension != ".hdr") ||
        (importAssetType == 0       ? textureSetting.colorSpace != ColorSpace::Srgb
             : importAssetType == 1 ? environmentSetting.colorSpace != ColorSpace::Srgb
                                    : true);
    const bool validSettings = importAssetType == 2
        ? meshPreview.ready() && asset_import::validId(meshSetting.id.value) &&
            std::isfinite(meshSetting.importScale) && meshSetting.importScale > 0.0f
        : importAssetType == 3
            ? asset_import::validId(sceneSetting.id.value) &&
                std::isfinite(sceneSetting.importScale) && sceneSetting.importScale > 0.0f
        : importAssetType != 1 ||
            (environmentSetting.irradianceSize > 0 && environmentSetting.irradianceSampleCount > 0 &&
             environmentSetting.prefilteredSpecularSampleCount > 0 &&
             environmentSetting.prefilteredSpecularMaxSampleCount >= environmentSetting.prefilteredSpecularSampleCount);
    if (!validSource)
    {
        ImGui::TextWrapped("Select an existing file in a supported format.");
    }
    else if (!validSettings || !validColorSpace)
    {
        ImGui::TextWrapped(
            "Check import settings. Mesh/Scene ID must be a valid filename; import scale must be finite and positive; "
            "EXR and HDR require Linear; sizes and sample counts must be positive, and the prefilter limit must cover "
            "the first mip."
        );
    }

    ImGui::BeginDisabled(!validSource || !validSettings || !validColorSpace);
    if (ImGui::Button("Confirm"))
    {
        std::string importedId;
        switch (importAssetType)
        {
            case 0:
                importedId = AssetImporter::importTexture(textureSetting).value;
                break;
            case 1:
                environmentImport.start(environmentSetting);
                importStatus = "Importing environment map (progress in console)...";
                break;
            case 2:
                importedId = AssetImporter::saveMesh(std::move(meshPreview.prepared()), meshSetting).value;
                meshPreview.clear();
                break;
            case 3:
                importedId = AssetImporter::importScene(sceneSetting).value;
                break;
            default:
                DCHECK(false, "invalid import asset type");
        }
        if (!environmentImport.busy())
        {
            importStatus = "Imported: " + importedId;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        meshPreview.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
void AssetImportPanel::shutdown() noexcept
{
    environmentImport.shutdown();
    meshPreview.clear();
}
