#include "core/GlobalState.h"
#include "core/Input.h"
#include "core/Logger.h"
#include "core/ScriptWriter.h"
#include "core/Scripting.h"
#include "core/Serializer.h"
#include "editor/EditorCameraConfig.h"
#include "editor/PassEditor.h"
#include "core/Settings.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/graph/RenderGraph.h"
#include "render/graph/RenderGraphImportExport.h"
#include "render/graph/RenderGraphUI.h"
#include "render/graph/RenderPassHelpers.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "core/Plugin.h"
#include "render/resources/Buffer.h"
#include "scene/SceneManager.h"
#include "scene/actor/ALight.h"
#include "scene/component/SpotLightComponent.h"

#include <imgui.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>

namespace
{
class LifecyclePass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(LifecyclePass, "LifecyclePass", "Records render pass lifecycle events.");

    explicit LifecyclePass(Falcor::ref<Falcor::Device> device) : Falcor::RenderPass(std::move(device)) {}

    Falcor::RenderPassReflection reflect(const CompileData&) override
    {
        Falcor::RenderPassReflection reflection;
        if (stateVisible)
            reflection.addInputOutput("state", {}).rawBuffer(stateSize);
        return reflection;
    }
    void setStateVisible(bool visible)
    {
        stateVisible = visible;
        requestRecompile();
    }
    void setStateSize(uint32_t size)
    {
        stateSize = size;
        requestRecompile();
    }
    void setCompileSize(uint32_t size)
    {
        compileSize = size;
        requestRecompile();
    }
    void compile(Falcor::RenderContext*, const CompileData&) override
    {
        ++compileCount;
        if (compileSize)
        {
            stateSize = *compileSize;
            compileSize.reset();
            LOG_ERROR("state width needs reflection");
        }
    }
    void execute(Falcor::RenderContext*, const Falcor::RenderData& data) override
    {
        const Falcor::ChannelList kChannels{
            {"state", "state", {}, true},
            {"missing", "missing", {}, true},
            {"required", "required", {}},
        };
        const auto defines = Falcor::getValidResourceDefines(kChannels, data);
        CHECK(defines.size() == 2);
        CHECK(defines.at("is_valid_state") == "1" && defines.at("is_valid_missing") == "0");

        auto& dictionary = data.getDictionary();
        const auto flags = dictionary.getValue(Falcor::kRenderPassRefreshFlags, Falcor::RenderPassRefreshFlags::None);
        dictionary[Falcor::kRenderPassRefreshFlags] = flags | Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
    }
    void renderUI(Falcor::Gui::Widgets&) override { showedUi = true; }
    void renderOverlayUI(Falcor::RenderContext*) override { showedOverlay = true; }
    void setScene(Falcor::RenderContext*, const Falcor::ref<Falcor::Scene>& next) override { scene = next; }
    void onSceneUpdates(Falcor::RenderContext*, Falcor::IScene::UpdateFlags changes) override { updates = changes; }
    bool onMouseEvent(const Falcor::MouseEvent& event) override { return event.type == Falcor::MouseEvent::Type::Move; }
    bool onKeyEvent(const Falcor::KeyboardEvent& event) override { return event.key == Key::F5; }
    void onHotReload(Falcor::HotReloadFlags flags) override { reloaded = flags == Falcor::HotReloadFlags::Program; }

    bool showedUi = false;
    bool showedOverlay = false;
    bool reloaded = false;
    uint32_t compileCount = 0;
    Falcor::ref<Falcor::Scene> scene;
    Falcor::IScene::UpdateFlags updates = Falcor::IScene::UpdateFlags::None;

private:
    bool stateVisible = true;
    uint32_t stateSize = 0;
    std::optional<uint32_t> compileSize;
};

void checkPassLifecycle(Falcor::Gui::Widgets& widgets, const std::filesystem::path& updatePath)
{
    using IOSize = Falcor::RenderPassHelpers::IOSize;
    CHECK(Falcor::math::all(Falcor::RenderPassHelpers::calculateIOSize(IOSize::Half, {}, {17, 9}) == Falcor::uint2{8, 4}));
    CHECK(Falcor::math::all(Falcor::RenderPassHelpers::calculateIOSize(IOSize::Default, {}, {17, 9}) == Falcor::uint2{}));
    CHECK(Serialize(IOSize::Quarter) == "Quarter");

    Falcor::Scripting::Context script;
    Falcor::setActivePythonRenderGraphDevice(Falcor::make_ref<Falcor::Device>());
    Falcor::Scripting::runScript("from falcor import RenderGraph, Resource, Buffer\ngraph = RenderGraph('Lifecycle')", script);
    Falcor::setActivePythonRenderGraphDevice({});
    const auto graph = script.getObject<Falcor::ref<Falcor::RenderGraph>>("graph");
    script.setObject("native_graph", graph);
    Falcor::Scripting::runScript("assert native_graph is graph", script);
    CHECK(std::string_view{graph->getClassName()} == "RenderGraph");
    const auto active = Falcor::make_ref<LifecyclePass>(graph->getDevice());
    const auto culled = Falcor::make_ref<LifecyclePass>(graph->getDevice());
    graph->addPass(active, "active");
    const auto first = Falcor::make_ref<Falcor::Scene>();
    graph->setScene(first);
    graph->addPass(culled, "culled");
    CHECK(graph->getScene() == first && active->scene == first && culled->scene == first);
    const auto second = Falcor::make_ref<Falcor::Scene>();
    graph->setScene(second);
    CHECK(graph->getScene() == second && active->scene == second && culled->scene == second);
    const auto buffer = Falcor::make_ref<Falcor::Buffer>();

    auto& context = *graph->getDevice()->getRenderContext();
    std::string log;
    CHECK(!graph->compile(&context, log) && log.contains("no marked outputs"));
    graph->markOutput("active.state");
    CHECK(!graph->compile(&context, log) && log.contains("missing graph input 'active.state'"));
    graph->setInput("active.state", buffer);
    CHECK(graph->compile(&context, log) && log.empty());
    active->setStateVisible(false);
    CHECK(!graph->compile(&context, log) && log.contains("unknown port 'active.state'"));
    active->setStateVisible(true);
    CHECK(graph->compile(&context, log) && log.empty());

    const auto initialCompileCount = active->compileCount;
    active->setCompileSize(sizeof(uint32_t));
    CHECK(graph->compile(&context, log) && log.empty());
    CHECK(active->compileCount == initialCompileCount + 2);
    active->setCompileSize(sizeof(uint32_t));
    CHECK(!graph->compile(&context, log) && log.contains("active: state width needs reflection"));
    CHECK(active->compileCount == initialCompileCount + 3);
    active->setStateSize(0);
    CHECK(graph->compile(&context, log) && log.empty());

    Falcor::Scripting::runScript(
        "original_name = Buffer.__name__\n"
        "Buffer.__name__ = 'DebugBuffer'\n"
        "output = graph.get_output(name='active.state')\n"
        "Buffer.__name__ = original_name\n"
        "assert isinstance(output, Buffer) and isinstance(output, Resource) and output.size == 0\n"
        "assert output.device is None and graph.get_output('active.state') is output\n"
        "assert graph.get_pass('active') is graph['active']",
        script
    );
    CHECK(script.getObject<Falcor::ref<Falcor::Resource>>("output") == buffer);
    auto& dictionary = graph->getPassesDictionary();
    dictionary[Falcor::kRenderPassRefreshFlags] = Falcor::RenderPassRefreshFlags::LightingChanged;
    graph->execute(&context);
    CHECK(dictionary.getValue<Falcor::RenderPassRefreshFlags>(Falcor::kRenderPassRefreshFlags) ==
          (Falcor::RenderPassRefreshFlags::LightingChanged | Falcor::RenderPassRefreshFlags::RenderOptionsChanged));
    graph->renderUI(&context, widgets);
    graph->renderOverlayUI(&context);
    CHECK(graph->onMouseEvent({.type = Falcor::MouseEvent::Type::Move}));
    CHECK(graph->onKeyEvent({.type = Falcor::KeyboardEvent::Type::KeyPressed, .key = Key::F5}));
    graph->onHotReload(Falcor::HotReloadFlags::Program);
    graph->onSceneUpdates(&context, Falcor::IScene::UpdateFlags::EnvMapChanged);
    CHECK(active->showedUi && active->showedOverlay && active->reloaded);
    CHECK(!culled->showedUi && !culled->showedOverlay && !culled->reloaded);
    CHECK(active->updates == Falcor::IScene::UpdateFlags::EnvMapChanged && culled->updates == active->updates);

    Falcor::RenderGraphUI editor(graph, graph->getName());
    editor.setRecordUpdates(true);
    uint32_t color = 0;
    CHECK(!editor.addLink("culled", "active", "state", "state", color));
    graph->setInput("culled.state", buffer);
    CHECK(!editor.addLink("culled", "active", {}, {}, color));
    CHECK(editor.addLink("culled", "active", "##outEx_culled", "##inEx_ active", color) && color);
    editor.updateGraph(&context);
    CHECK(!editor.addLink("active", "culled", "##outEx_active", "##inEx_ culled", color));

    Falcor::Gui gui;
    active->setStateVisible(true);
    editor.renderUI(&context, &gui);
    CHECK(graph->getOutput("active.state") == buffer);

    editor.removeEdge("culled", "active", "##outEx_culled", "##inEx_ active");
    editor.removeOutput("active", "state");
    editor.updateGraph(&context);
    CHECK(!graph->isGraphOutput("active.state") && editor.getCurrentLog().contains("no marked outputs"));
    editor.writeUpdateScriptToFile(&context, updatePath, 3.0f);
    std::error_code error;
    CHECK(!std::filesystem::exists(updatePath, error) && !error);
    editor.addOutput("active.state");
    editor.updateGraph(&context);
    CHECK(graph->getOutput("active.state") == buffer);

    graph->setInput("culled.state", {});
    CHECK(editor.addLink("active", "culled", "state", "state", color));
    editor.addOutput("culled", "state");
    editor.updateGraph(&context);
    CHECK(graph->getOutput("culled.state") == buffer);

    active->setStateSize(sizeof(uint32_t));
    culled->setStateSize(2 * sizeof(uint32_t));
    CHECK(!graph->compile(&context, log) && log.contains("incompatible width"));
    CHECK(log.contains("cannot connect 'active.state' to 'culled.state'"));
    culled->setStateSize(sizeof(uint32_t));
    CHECK(graph->compile(&context, log) && graph->getOutput("culled.state") == buffer);

    Falcor::PluginManager::instance().loadPluginByName("ToneMappingPass");
    editor.addRenderPass("tone", "ToneMappingPass");
    editor.updateGraph(&context);
    CHECK(graph->doesPassExist("tone"));
    editor.removeRenderPass("tone");
    editor.updateGraph(&context);
    CHECK(!graph->doesPassExist("tone"));

    editor.writeUpdateScriptToFile(&context, updatePath, 3.0f);
    std::ifstream updates(updatePath);
    CHECK(updates.is_open());
    const std::string commands{std::istreambuf_iterator<char>{updates}, std::istreambuf_iterator<char>{}};
    CHECK(commands.contains("g.add_edge") && commands.contains("g.remove_edge") &&
          commands.contains("g.create_pass") && commands.contains("g.unmark_output"));
    updates.close();
    CHECK(std::filesystem::remove(updatePath, error), "cannot remove '{}': {}", updatePath.string(), error.message());
    editor.clearCurrentLog();
    CHECK(editor.getCurrentLog().empty());

    graph->removePass("culled");
    CHECK(graph->compile(&context, log));
    const auto compileCountAfterRemoval = active->compileCount;
    culled->setStateVisible(true);
    CHECK(graph->compile(&context, log) && active->compileCount == compileCountAfterRemoval);

    for (const bool removeSourcePass : {false, true})
    {
        auto source = Falcor::RenderGraph::create(graph->getDevice(), "Source");
        source->addPass(culled, "culled");
        source->markOutput("culled.state");

        Falcor::RenderGraph transient(graph->getDevice(), "Transient");
        transient.update(source);
        transient.setInput("culled.state", buffer);
        CHECK(transient.compile(&context, log));

        if (removeSourcePass)
            source->removePass("culled");
        source.reset();
        culled->setStateVisible(false);
        CHECK(!transient.compile(&context, log) && log.contains("unknown port 'culled.state'"));
        culled->setStateVisible(true);
        CHECK(transient.compile(&context, log) && log.empty());
    }
    culled->setStateVisible(false);
}

void checkSceneMetadata()
{
    SceneManager scenes;
    scenes.init();

    SpotLightComponent spot;
    const auto         fields = Serialize(spot);
    CHECK(fields.contains("color") && fields.contains("luminousIntensity") && fields.contains("cosOuter"));
    CHECK(fields.at("offlineEmission").is_null());

    SceneAsset scene;
    scene.actors.push_back(std::make_unique<ALight>(std::make_unique<SpotLightComponent>()));
    const auto saved = Serialize(scene);
    SceneAsset loaded;
    Deserialize(saved, loaded);
    CHECK(loaded.actors.size() == 1);
    CHECK(loaded.actors.front()->getComponent<SpotLightComponent>());
    CHECK(Serialize(loaded) == saved);

    const auto flags = Serialize(Modifier::Shift | Modifier::Alt);
    CHECK(flags == nlohmann::json::array({"Shift", "Alt"}));
    Modifier restored;
    Deserialize(flags, restored);
    CHECK(restored == (Modifier::Shift | Modifier::Alt));

    scenes.shutdown();
}

void checkPluginEditor(Falcor::PluginManager& plugins, const std::filesystem::path& updatePath)
{
    const auto device = Falcor::make_ref<Falcor::Device>();
    const auto pass = Falcor::RenderPass::create("ToneMappingPass", device, {}, plugins);
    const VulkanContext      vulkan;
    const EditorCameraConfig camera;
    bool submitted = false;
    const PassEditorContext context{
        .vulkan = vulkan,
        .camera = camera,
        .submit = [&](const Falcor::RenderPass&, std::optional<nlohmann::json>) { submitted = true; },
    };
    auto renderCommands = *device->getRenderContext();
    renderCommands.passEditor = &context;
    ImGui::CreateContext();
    auto& io       = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {320.0f, 240.0f};
    CHECK(io.Fonts->Build());

    ImGui::NewFrame();
    ImGui::Begin("Pass options");
    Falcor::Gui::Widgets widgets;
    ImGui::SetNextItemOpen(true);
    pass->renderUI(&renderCommands, widgets);
    CHECK(!submitted);
    checkPassLifecycle(widgets, updatePath);
    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext();
}
struct PropertyRecord
{
    uint32_t sampleCount = 4;
    std::string label = "test";

    template <typename Archive>
    void serialize(Archive& archive)
    {
        archive("sampleCount", sampleCount);
        archive("label", label);
    }
};

void checkProperties()
{
    auto properties = Falcor::PropertiesWriter::write(PropertyRecord{});
    properties["child"] = PropertyRecord{.sampleCount = 2};
    const auto copy = properties;
    for (auto [name, value] : properties)
        if (name == "sampleCount")
            value = 8u;

    const auto [name, value] = *copy.begin();
    CHECK(name == "sampleCount" && static_cast<uint32_t>(value) == 4);
    CHECK(copy != properties && properties.get("sampleCount", 0u) == 8);
    CHECK(properties.get("missing", 3u) == 3 && !properties.getOpt<uint32_t>("missing"));
    const auto child = properties.getOpt<PropertyRecord>("child");
    CHECK(child && child->sampleCount == 2 && child->label == "test");
    CHECK(Falcor::PropertiesReader::read<PropertyRecord>(properties).sampleCount == 8);
}

void checkScripting()
{
    CHECK(!Falcor::Scripting::isRunning());
    Falcor::ScriptBindings::registerBinding([](PyObject* m)
    {
        if (Falcor::ScriptBindings::resolveDeferredBinding("BindingDependency", m) != 0)
            return -1;
        const Falcor::PythonObject value{PyObject_GetAttrString(m, "binding_dependency")};
        return value ? PyModule_AddObjectRef(m, "deferred_binding", value.get()) : -1;
    });
    Falcor::ScriptBindings::registerDeferredBinding("BindingDependency", [](PyObject* m)
    {
        CHECK(!PyObject_HasAttrString(m, "binding_dependency"));
        return PyModule_AddIntConstant(m, "binding_dependency", 42);
    });
    CHECK(Falcor::Scripting::interpretScript("1 + 2") == "3\n");
    CHECK(Falcor::Scripting::isRunning() && Falcor::Scripting::getDefaultContext().containsObject("RenderGraph"));
    CHECK(Falcor::Scripting::getDefaultContext().getObject<int>("deferred_binding") == 42);
    Falcor::ScriptBindings::registerBinding([](PyObject* m) { return PyModule_AddIntConstant(m, "runtime_binding", 43); });
    CHECK(Falcor::ScriptWriter::makeSetProperty("settings", "samples", 4u) == "settings.samples = 4\n");
    CHECK(Falcor::ScriptWriter::makeFunc("create", "graph", true) == "create('graph', True)\n");
    CHECK(to_string(Falcor::ResourceFormat::RGBA16Float) == "RGBA16Float");
    CHECK(Falcor::getFormatRowPitch(Falcor::ResourceFormat::BC1Unorm, 8) == 16);
    CHECK(Falcor::isIntegerFormat(Falcor::ResourceFormat::RGB10A2Uint) && !Falcor::isIntegerFormat(Falcor::ResourceFormat::R32Float));

    {
        Falcor::Scripting::Context script;
        constexpr auto kMask = Falcor::TextureChannelFlags::Red | Falcor::TextureChannelFlags::Alpha;
        constexpr auto kBindFlags = Falcor::ResourceBindFlags::ShaderResource | Falcor::ResourceBindFlags::RenderTarget;
        constexpr auto kFormat = Falcor::ResourceFormat::RGBA16Float;
        script.setObject("mask", kMask);
        script.setObject("bindFlags", kBindFlags);
        script.setObject("format", kFormat);
        CHECK(script.getObject<Falcor::TextureChannelFlags>("mask") == kMask);
        CHECK(script.getObject<Falcor::ResourceBindFlags>("bindFlags") == kBindFlags);
        CHECK(script.getObject<Falcor::ResourceFormat>("format") == kFormat);
        CHECK(Falcor::ScriptWriter::getArgString(kMask) == "TextureChannelFlags(9)");
        Falcor::Scripting::runScript(
            "from falcor import Buffer, Resource, Texture, TextureChannelFlags, runtime_binding\n"
            "from falcor import ResourceBindFlags, ResourceFormat\n"
            "assert issubclass(Buffer, Resource) and issubclass(Texture, Resource)\n"
            "assert Texture.__basicsize__ == Resource.__basicsize__\n"
            "assert bindFlags == ResourceBindFlags.ShaderResource | ResourceBindFlags.RenderTarget\n"
            "assert format is ResourceFormat.RGBA16Float\n"
            "assert runtime_binding == 43 and isinstance(mask, TextureChannelFlags)",
            script
        );

        static PyMethodDef kSetValue{
            "set_value",
            [](PyObject*, PyObject*) -> PyObject*
            {
                Falcor::Scripting::getCurrentContext().setObject("value", std::string("recorded"));
                Py_RETURN_NONE;
            },
            METH_NOARGS,
        };
        const Falcor::PythonObject setValue{PyCFunction_New(&kSetValue, nullptr)};
        CHECK(setValue, "{}", Falcor::pythonError());
        script.setObject(kSetValue.ml_name, setValue.get());

        const Falcor::PythonObject stdoutBefore{Py_NewRef(PySys_GetObject("stdout"))};
        const Falcor::PythonObject stderrBefore{Py_NewRef(PySys_GetObject("stderr"))};
        const auto captured = Falcor::Scripting::runScript(
            "import sys\nset_value()\nprint(value)\nprint('diagnostic', file=sys.stderr)", script, true
        );
        CHECK(captured.out == "recorded\n" && captured.err == "diagnostic\n");
        CHECK(script.getObject<std::string>("value") == "recorded");
        CHECK(PySys_GetObject("stdout") == stdoutBefore.get() && PySys_GetObject("stderr") == stderrBefore.get());
        const auto uncaptured = Falcor::Scripting::runScript("assert value == 'recorded'", script);
        CHECK(uncaptured.out.empty() && uncaptured.err.empty());
    }

    Falcor::Scripting::shutdown();
    CHECK(!Falcor::Scripting::isRunning());
    CHECK(Falcor::Scripting::getDefaultContext().getObject<int>("deferred_binding") == 42);
}
}  // namespace

int main(int, char** argv)
{
    logger::init(argv[0]);
    checkScripting();
    checkProperties();
    checkSceneMetadata();
    Falcor::PluginManager plugins;
    CHECK(!plugins.hasClass<Falcor::RenderPass>("RayTracingPass"));
    const auto first    = Falcor::RenderPass::create("RayTracingPass", {}, {}, plugins);
    CHECK(plugins.hasClass<Falcor::RenderPass>("RayTracingPass"));
    plugins.loadAllPlugins();
    const auto second   = Falcor::RenderPass::create("RayTracingPass", {}, {}, plugins);
    const auto minimal  = Falcor::RenderPass::create("MinimalPathTracer", {}, {}, plugins);
    const auto pt       = Falcor::RenderPass::create("PathTracer", {}, {}, plugins);
    const auto bdpt     = Falcor::RenderPass::create("BDPT", {}, {}, plugins);
    const auto defaults = first->getProperties();
    const auto graph    = Falcor::RenderGraph::create({}, "Pass factory");
    const auto traced   = graph->createPass("trace", "MinimalPathTracer", Falcor::Properties::json{{"maxBounces", 4}});
    CHECK(graph->getPass("trace") == traced && traced->getProperties().get<uint32_t>("maxBounces") == 4);
    const auto device = Falcor::make_ref<Falcor::Device>();
    const auto previousDevice = Falcor::make_ref<Falcor::Device>();
    CHECK(device->getRenderContext() != previousDevice->getRenderContext());
    CHECK(device->getRenderContext()->getDevice() == device);
    CHECK(previousDevice->getRenderContext()->getDevice() == previousDevice);
    Falcor::setActivePythonRenderGraphDevice(previousDevice);
    CHECK(!Falcor::RenderGraph::createFromString(device, "pass"));
    CHECK(Falcor::getActivePythonRenderGraphDevice() == previousDevice);
    const auto imported = Falcor::RenderGraph::createFromString(device, R"(
from falcor import RenderGraph, TextureChannelFlags
from types import SimpleNamespace
assert isinstance(m, SimpleNamespace) and set(vars(m)) == {"addGraph"}
assert "addGraph" not in globals()
g = RenderGraph("Scripted Graph")
g.create_pass("trace", "MinimalPathTracer", {"maxBounces": 4})
g.update_pass("trace", {"maxBounces": 5})
trace = g["trace"]
trace.set_properties(trace.properties)
try:
    trace.set_properties({"maxBounces": object()})
except TypeError:
    pass
else:
    raise AssertionError("invalid properties must raise TypeError")
assert trace.properties["maxBounces"] == 5
assert g.get_pass(name="trace").type == "MinimalPathTracer"
assert g.get_pass(name="trace").desc == "Minimal path tracer."
g.create_pass("tone", "ToneMappingPass", {"mode": "Neutral"})
g.add_edge("trace.color", "tone.hdrColor")
g.mark_output("trace.color")
g.mark_output("trace.color", TextureChannelFlags.Alpha)
try:
    g.mark_output("trace.color", 8)
except TypeError:
    pass
else:
    raise AssertionError("mark_output accepts TextureChannelFlags")
try:
    m.addGraph(None)
except TypeError:
    pass
else:
    raise AssertionError("addGraph accepts a RenderGraph")
m.addGraph(RenderGraph("Earlier Graph"))
m.addGraph(g)
)");
    CHECK(imported && imported->getDevice() == device && imported->isGraphOutput("trace.color"));
    CHECK(Falcor::getActivePythonRenderGraphDevice() == previousDevice);
    Falcor::setActivePythonRenderGraphDevice({});
    CHECK(imported->getPass("trace")->getProperties().get<uint32_t>("maxBounces") == 5);
    CHECK(imported->getOutputMasks(0).contains(Falcor::TextureChannelFlags::Alpha));
    const auto live = Falcor::RenderGraph::create(device);
    const auto kept = live->createPass("trace", "MinimalPathTracer", Falcor::Properties::json{{"maxBounces", 3}});
    live->update(imported);
    CHECK(live->getPass("trace") == kept && kept->getProperties().get<uint32_t>("maxBounces") == 3);
    CHECK(live->doesPassExist("tone") && live->isGraphOutput("trace.color"));

    const auto bloom = Falcor::RenderPass::create("Bloom", {}, {}, plugins);
    auto bloomProperties = bloom->getProperties();
    bloomProperties.set("intensity", 0.1f);
    bloom->setProperties(bloomProperties);
    CHECK(bloom->getProperties() == bloomProperties);
    bloomProperties.set("levels", bloomProperties.get<uint32_t>("levels") - 1);
    bloom->setProperties(bloomProperties);
    CHECK(bloom->getProperties() == bloomProperties);

    std::error_code error;
    const auto temp = std::filesystem::temp_directory_path(error);
    CHECK(!error, "cannot find temporary directory: {}", error.message());
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = temp / std::format("RenderLabGraph-{}.{}", suffix, Falcor::RenderGraph::kFileExtensionFilters.front().ext);
    const auto expected = Falcor::RenderGraphExporter::getIR(imported);
    CHECK(Falcor::RenderGraphExporter::save(imported, path));
    Falcor::setActivePythonRenderGraphDevice(previousDevice);
    CHECK(Falcor::RenderGraphExporter::getIR(Falcor::RenderGraph::createFromFile(device, path)) == expected);
    CHECK(Falcor::getActivePythonRenderGraphDevice() == previousDevice);
    Falcor::setActivePythonRenderGraphDevice({});

    constexpr auto kSecondGraphName = "secondGraph";
    {
        std::ofstream file(path, std::ios::app);
        CHECK(file, "cannot append to '{}'", path.string());
        file << kSecondGraphName << " = " << Falcor::RenderGraphExporter::getFuncName(imported->getName()) << "()\n";
        file << "emptyGraph = None\n";
        file << "if __name__ == 'CaptureTest':\n    import sys\n    print('file output')\n    sys.path = []\n";
        file.close();
        CHECK(file, "cannot write '{}'", path.string());
    }

    Falcor::setActivePythonRenderGraphDevice(device);
    const auto selected = Falcor::RenderGraphImporter::import(imported->getName(), path);
    const auto all = Falcor::RenderGraphImporter::importAllGraphs(path);
    {
        Falcor::Scripting::Context script;
        script.setObject("__name__", std::string("CaptureTest"));
        const Falcor::PythonObject searchPath{Py_NewRef(PySys_GetObject("path"))};
        const auto captured = Falcor::Scripting::runScriptFromFile(path, script, true);
        CHECK(captured.out == "file output\n" && captured.err.empty());
        CHECK(PySys_GetObject("path") == searchPath.get());
    }
    Falcor::setActivePythonRenderGraphDevice({});
    CHECK(Falcor::RenderGraphExporter::getIR(selected) == expected);
    CHECK(all.size() == 2 && Falcor::RenderGraphExporter::getIR(all.front()) == expected);
    CHECK(all.back()->getName() == kSecondGraphName);
    CHECK(!Falcor::Scripting::getDefaultContext().containsObject(imported->getName()));
    CHECK(std::filesystem::remove(path, error), "cannot remove '{}': {}", path.string(), error.message());
    LOG_INFO("Render graph script export and import round trip passed");

    CHECK(defaults == second->getProperties());
    const auto infos = plugins.getInfos<Falcor::RenderPass>();
    const auto info = std::ranges::find_if(infos, [](const auto& entry) { return entry.first == "MinimalPathTracer"; });
    CHECK(info != infos.end() && info->second.desc == minimal->getDesc());
    checkPluginEditor(plugins, path);

    auto scripted                        = defaults.toJson();
    scripted["restir"]["candidateCount"] = 16;
    first->setProperties(scripted);
    CHECK(second->getProperties() == defaults);

    auto edited                       = first->getProperties().toJson();
    edited["restir"]["temporalReuse"] = false;
    Settings settings;
    settings.addOption("pass", nlohmann::json(edited));
    settings.addOption("passOther", nlohmann::json(scripted));
    CHECK(!settings.getOption("pass:restir:temporalReuse", true));
    settings.addOption("pass", nullptr);
    CHECK(settings.getOption("pass:restir:temporalReuse", true));
    CHECK(settings.getOption<uint32_t>("passOther:restir:candidateCount") == 16);
    settings.clearOptions();
    CHECK(!settings.getOptions().has("passOther:restir:candidateCount"));
    first->setProperties(edited);

    CHECK((minimal->getProperties().get<uint32_t>("maxBounces") == 3));
    minimal->setProperties(Falcor::Properties::json{{"maxBounces", 5}});
    CHECK((pt->getProperties().get<uint32_t>("maxBounces") == 2));
    CHECK(bdpt->getProperties().get<bool>("lightTracing") == true);
    auto bdptSettings = bdpt->getProperties();
    bdptSettings.set("misHeuristic", std::string{"Power"});
    bdpt->setProperties(bdptSettings);
    first->setProperties(scripted);
    CHECK(first->getProperties().toJson() == scripted);

    plugins.releaseAllPlugins();
    CHECK(!plugins.hasClass<Falcor::RenderPass>("BDPT"));
    CHECK(minimal->getDesc() == "Minimal path tracer.");
    CHECK(first->getProperties().toJson() == scripted);
    CHECK(second->getProperties() == defaults);
    CHECK(minimal->getProperties().get<uint32_t>("maxBounces") == 5);
    CHECK(bdpt->getProperties() == bdptSettings);
    LOG_INFO("Dynamic pass factories, independent properties, editor registration, and plugin lifetime passed");
}
