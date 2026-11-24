#include "editor/Editor.h"

#include <slang-rhi.h>
#include "asset/Asset.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "editor/EditorCamera.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"
#include "scene/component/TransformComponent.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include <glm/glm.hpp>

namespace
{
constexpr std::string_view kSceneObjectsEnabled = "SceneObjects:enabled";
}

Editor::~Editor() = default;

void Editor::init()
{

    editorCamera    = std::make_unique<EditorCamera>(context().sceneManager->scene().editorCamera);
    sceneGeneration = context().sceneManager->sceneGeneration();
    initBackend();
}

void Editor::initBackend()
{
    backend.init(
        context().window->nativeHandle(),
        context().renderer->getDevice(),
        context().renderer->getShaderManager(),
        context().renderer->getSurface()->getConfig()->format
    );
}

void Editor::shutdown() noexcept
{
    if (windowHasObjectList)
    {
        auto&       config = *context().config;
        const auto& size   = config.editorConfig();
        config.setEditorSize(std::max(size.width - ui.objectListInset(size.width), 1), size.height);
        windowHasObjectList = false;
    }
    editorCamera->resetNavigation();
    editorCamera.reset();
    ui.resetLayout();
    backend.shutdown();
    selectedId = 0;
    dirty      = false;
    pendingSceneId.reset();
    pendingObjectActions = {};
}

void Editor::refreshUi()
{
    backend.shutdown();
    ui.resetLayout();
    initBackend();
}

bool Editor::resizeWindowForLayout()
{
    Context&   ctx            = context();
    const bool showObjectList = ctx.config->getSettings().getOption(kSceneObjectsEnabled, false);
    if (showObjectList == windowHasObjectList)
        return false;

    auto& window               = *ctx.window;
    const auto [width, height] = window.size();
    const int inset            = ui.objectListInset(width);
    const int nextWidth        = std::max(width + (showObjectList ? inset : -inset), 1);
    window.resizeCentered(nextWidth, height);
    windowHasObjectList = showObjectList;
    return true;
}

void Editor::saveScene()
{
    Context& ctx = context();

    ctx.sceneManager->save();
    dirty = false;
}

void Editor::applySceneActions()
{
    if (!pendingSceneId && !pendingObjectActions.createType && !pendingObjectActions.deleteId)
        return;

    auto& scene = *context().sceneManager;

    if (pendingSceneId)
    {
        scene.load(*pendingSceneId);
        selectedId = 0;
        dirty      = false;
        pendingSceneId.reset();
        pendingObjectActions = {};
        return;
    }

    if (pendingObjectActions.deleteId)
    {
        scene.deleteActor(pendingObjectActions.deleteId);
        if (selectedId == pendingObjectActions.deleteId)
            selectedId = 0;
    }
    if (pendingObjectActions.createType)
        selectedId = scene.createActor(*pendingObjectActions.createType).selectionId();

    pendingObjectActions = {};
    dirty                = true;
}

Editor::Frame Editor::buildFrame(const EditorCamera& camera)
{
    backend.beginFrame();
    ui.beginFrame(windowHasObjectList);
    auto& scene          = context().sceneManager->scene();
    pendingObjectActions = ui.drawHierarchy(scene, selectedId);
    if (pendingObjectActions.renamed)
        dirty = true;

    const auto viewport      = ui.sceneViewportPixels();
    auto*      selectedActor = context().sceneManager->findActor(selectedId);
    if (!camera.isFreeMovementActive() && selectedActor != nullptr)
    {
        const auto sceneCamera = camera.config().sceneCamera();
        const auto view        = sceneCamera.viewMatrix();
        const auto projection  = sceneCamera.projectionMatrix(viewport.width / viewport.height);
        if (ui.drawGizmo(selectedActor->transform(), view, projection, !camera.isNavigationActive()))
        {
            dirty = true;
        }
    }

    EditorUI::InspectorResult inspector = ui.drawInspector(scene, selectedActor, dirty);
    pendingSceneId                      = std::move(inspector.sceneId);
    if (inspector.edited)
        dirty = true;
    if (!pendingSceneId && ui.saveRequested())
        saveScene();

    Frame frame{.viewport = viewport};
    auto& input    = frame.input;
    input.recordUi = [this](rhi::IRenderPassEncoder& commands, uint32_t frameIndex)
    { backend.render(commands, frameIndex); };

    const auto& config   = *context().config;
    const auto& renderer = *context().renderer;
    const auto* graph    = renderer.getActiveGraph();
    if (config.editorConfig().objectPicking && !camera.isNavigationActive() && graph &&
        graph->getName() == config.renderGraphName())
    {
        glm::vec2 viewportUv;
        if (ui.sceneClicked(viewportUv) && graph->hasActivePass(kEditorPickingPassType))
        {
            input.pick = EditorPickRequest{
                .viewportUv = viewportUv,
                .complete   = [this](uint32_t selectionId) { selectedId = selectionId; },
            };
        }
    }

    ui.endFrame();
    return frame;
}

bool Editor::wantsInput() const { return ui.wantsInput(); }

void Editor::tick(float deltaTime)
{
    const auto generation = context().sceneManager->sceneGeneration();
    if (generation != sceneGeneration)
    {
        editorCamera->resetNavigation();
        selectedId      = 0;
        dirty           = false;
        sceneGeneration = generation;
    }
    editorCamera->setInputEnabled(editorCamera->isNavigationActive() || !wantsInput());
    editorCamera->tick(deltaTime);
}
