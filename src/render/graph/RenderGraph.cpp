#include "render/graph/RenderGraph.h"

#include <slang-rhi.h>
#include "core/DirectedGraphTraversal.h"
#include "core/GlobalState.h"
#include "core/Logger.h"
#include "core/ScriptBindings.h"
#include "core/Scripting.h"
#include "render/RenderContext.h"

#include <algorithm>
#include <functional>
#include <type_traits>
#include <utility>

const FileDialogFilterVec RenderGraph::kFileExtensionFilters{{"py", "Render Graph Files"}};

namespace
{
    auto makePassChangedCB(const ref<RenderPass>& slot, bool& recompile)
    {
        struct Callback
        {
            const ref<RenderPass>* slot;
            bool*                  recompile;

            void operator()() const { *recompile = true; }
        };
        return Callback{&slot, &recompile};
    }

    void clearPassChangedCB(std::function<void()>& callback, const ref<RenderPass>& slot)
    {
        using Callback = decltype(makePassChangedCB(slot, std::declval<bool&>()));
        // update() can attach the same Pass to another graph's node.
        if (const auto* target = callback.target<Callback>(); target && target->slot == &slot)
            callback = [] {};
    }
}  // namespace

RenderGraph::RenderGraph(rhi::ComPtr<rhi::IDevice> device, const std::string& name) :
    mpDevice(std::move(device)), mName(name), mpGraph(std::make_unique<DirectedGraph>())
{
}

RenderGraph::~RenderGraph()
{
    for (const auto& [index, node] : mNodeData)
        clearPassChangedCB(node.pPass->mPassChangedCB, node.pPass);
}

ref<RenderGraph> RenderGraph::create(rhi::ComPtr<rhi::IDevice> device, const std::string& name)
{
    return make_ref<RenderGraph>(std::move(device), name);
}

namespace
{
    PyMethodDef kAddGraph{
        "addGraph",
        [](PyObject* result, PyObject* graph) -> PyObject*
        {
            if (!ScriptBindings::isInstance(graph, typeid(ref<RenderGraph>)))
            {
                PyErr_SetString(PyExc_TypeError, "m.addGraph expects a RenderGraph");
                return nullptr;
            }
            if (PyList_SetItem(result, 0, Py_NewRef(graph)) != 0)
                return nullptr;
            Py_RETURN_NONE;
        },
        METH_O,
        nullptr,
    };

}  // namespace

ref<RenderGraph> RenderGraph::createFromFile(rhi::ComPtr<rhi::IDevice> device, const std::filesystem::path& path)
{
    Scripting::Context context;
    const PythonObject result{Py_BuildValue("[O]", Py_None)};
    CHECK(result, "{}", pythonError());
    const PythonObject addGraph{PyCFunction_New(&kAddGraph, result.get())};
    const PythonObject types{PyImport_ImportModule("types")};
    CHECK(addGraph && types, "{}", pythonError());
    const PythonObject m{PyObject_CallMethod(types.get(), "SimpleNamespace", nullptr)};
    CHECK(m && PyObject_SetAttrString(m.get(), "addGraph", addGraph.get()) == 0, "{}", pythonError());
    context.setObject("m", m.get());

    auto previous = getActivePythonRenderGraphDevice();
    setActivePythonRenderGraphDevice(std::move(device));
    Scripting::runScriptFromFile(path, context);
    setActivePythonRenderGraphDevice(std::move(previous));

    return ScriptBindings::fromPython<ref<RenderGraph>>(PyList_GET_ITEM(result.get(), 0)).value_or(ref<RenderGraph>{});
}

ref<RenderGraph> RenderGraph::createFromString(rhi::ComPtr<rhi::IDevice> device, std::string_view script)
{
    Scripting::Context context;
    const PythonObject result{Py_BuildValue("[O]", Py_None)};
    CHECK(result, "{}", pythonError());
    const PythonObject addGraph{PyCFunction_New(&kAddGraph, result.get())};
    const PythonObject types{PyImport_ImportModule("types")};
    CHECK(addGraph && types, "{}", pythonError());
    const PythonObject m{PyObject_CallMethod(types.get(), "SimpleNamespace", nullptr)};
    CHECK(m && PyObject_SetAttrString(m.get(), "addGraph", addGraph.get()) == 0, "{}", pythonError());
    context.setObject("m", m.get());

    auto previous = getActivePythonRenderGraphDevice();
    setActivePythonRenderGraphDevice(std::move(device));
    Scripting::runScript(script, context);
    setActivePythonRenderGraphDevice(std::move(previous));

    return ScriptBindings::fromPython<ref<RenderGraph>>(PyList_GET_ITEM(result.get(), 0)).value_or(ref<RenderGraph>{});
}

ref<RenderPass>
RenderGraph::createPass(const std::string& passName, const std::string& passType, const Properties& properties)
{
    auto pass = RenderPass::create(passType, mpDevice, properties);
    addPass(pass, passName);
    return pass;
}

namespace
{
    std::pair<std::string, std::string> parseFieldName(const std::string& name)
    {
        const auto dot = name.rfind('.');
        if (dot == std::string::npos)
            return {name, {}};

        CHECK(dot != 0 && dot + 1 < name.size(), "invalid resource endpoint '{}'", name);
        return {name.substr(0, dot), name.substr(dot + 1)};
    }
}  // namespace

RenderPass* RenderGraph::getRenderPassAndNamePair(
    bool input, const std::string& fullName, std::pair<std::string, std::string>& nameAndField
) const
{
    nameAndField              = parseFieldName(fullName);
    const auto& [name, field] = nameAndField;
    auto* pass                = getPass(name).get();
    if (!field.empty())
    {
        using Visibility       = RenderPassReflection::Field::Visibility;
        const auto& defaults   = mCompilerDeps.defaultResourceProps;
        const auto  reflection = pass->reflect({defaults.dims, defaults.format});
        const auto* port       = reflection.getField(field);
        CHECK(
            port && is_set(port->getVisibility(), input ? Visibility::Input : Visibility::Output),
            "unknown {} port '{}'",
            input ? "input" : "output",
            fullName
        );
    }
    return pass;
}

uint32_t RenderGraph::getPassIndex(const std::string& name) const
{
    const auto found = mNameToIndex.find(name);
    return found == mNameToIndex.end() ? kInvalidIndex : found->second;
}

uint32_t RenderGraph::addPass(const ref<RenderPass>& pass, const std::string& name)
{
    CHECK(pass);
    CHECK(!name.empty(), "invalid render pass instance name '{}'", name);
    CHECK(!doesPassExist(name), "duplicate render pass '{}'", name);

    const auto index = mpGraph->addNode();
    mNameToIndex.emplace(name, index);
    const auto& node     = mNodeData.emplace(index, NodeData{name, pass}).first->second;
    pass->mName          = name;
    pass->mPassChangedCB = makePassChangedCB(node.pPass, mRecompile);
    mRecompile = true;
    return index;
}

void RenderGraph::removePass(const std::string& name)
{
    const auto index = getPassIndex(name);
    CHECK(index != kInvalidIndex, "unknown render pass '{}'", name);

    mRecompile = true;
    for (const auto edgeId : mpGraph->removeNode(index))
        mEdgeData.erase(edgeId);
    std::erase_if(mOutputs, [index](const auto& output) { return output.nodeId == index; });
    const auto prefix = name + '.';
    std::erase_if(
        mCompilerDeps.externalResources,
        [&](const auto& input) { return input.first.rfind('.') == name.size() && input.first.starts_with(prefix); }
    );
    const auto& node = mNodeData.at(index);
    clearPassChangedCB(node.pPass->mPassChangedCB, node.pPass);
    mNodeData.erase(index);
    mNameToIndex.erase(name);
}

void RenderGraph::updatePass(const std::string& name, const Properties& properties)
{
    const auto index = getPassIndex(name);
    CHECK(index != kInvalidIndex, "unknown render pass '{}'", name);
    auto& node = mNodeData.at(index);
    auto  pass = RenderPass::create(node.pPass->getType(), mpDevice, properties);
    clearPassChangedCB(node.pPass->mPassChangedCB, node.pPass);
    const auto previous  = std::exchange(node.pPass, pass);
    pass->mName          = name;
    pass->mPassChangedCB = makePassChangedCB(node.pPass, mRecompile);
    mRecompile = true;
}

const ref<RenderPass>& RenderGraph::getPass(const std::string& name) const
{
    const auto index = getPassIndex(name);
    CHECK(index != kInvalidIndex, "unknown render pass '{}'", name);
    return mNodeData.at(index).pPass;
}

uint32_t RenderGraph::getEdge(const std::string& source, const std::string& target)
{
    const auto [srcPass, srcField] = parseFieldName(source);
    const auto [dstPass, dstField] = parseFieldName(target);
    const auto sourceId            = getPassIndex(srcPass);
    const auto targetId            = getPassIndex(dstPass);
    if (sourceId == kInvalidIndex || targetId == kInvalidIndex)
        return kInvalidIndex;

    const auto& node = *mpGraph->getNode(sourceId);
    for (uint32_t index = 0; index < node.getOutgoingEdgeCount(); ++index)
    {
        const auto  edgeId = node.getOutgoingEdge(index);
        const auto& data   = mEdgeData.at(edgeId);
        if (mpGraph->getEdge(edgeId)->getDestNode() == targetId && data.srcField == srcField &&
            data.dstField == dstField)
            return edgeId;
    }
    return kInvalidIndex;
}

uint32_t RenderGraph::addEdge(const std::string& source, const std::string& target)
{
    std::pair<std::string, std::string> src;
    std::pair<std::string, std::string> dst;
    getRenderPassAndNamePair(false, source, src);
    getRenderPassAndNamePair(true, target, dst);
    auto& [srcPass, srcField] = src;
    auto& [dstPass, dstField] = dst;
    const auto sourceId       = getPassIndex(srcPass);
    const auto targetId       = getPassIndex(dstPass);
    CHECK(sourceId != targetId && (srcField.empty() == dstField.empty()), "invalid edge '{}' -> '{}'", source, target);
    if (!dstField.empty())
    {
        const auto& node = *mpGraph->getNode(targetId);
        for (uint32_t index = 0; index < node.getIncomingEdgeCount(); ++index)
            CHECK(
                mEdgeData.at(node.getIncomingEdge(index)).dstField != dstField,
                "input '{}' already has an edge",
                target
            );
    }
    CHECK(
        !DirectedGraphPathDetector::hasPath(*mpGraph, targetId, sourceId),
        "edge '{}' -> '{}' introduces a cycle",
        source,
        target
    );

    const auto edgeId = mpGraph->addEdge(sourceId, targetId);
    mEdgeData.emplace(edgeId, EdgeData{std::move(srcField), std::move(dstField)});
    mRecompile = true;
    return edgeId;
}

void RenderGraph::removeEdge(const std::string& source, const std::string& target)
{
    const auto edgeId = getEdge(source, target);
    CHECK(edgeId != kInvalidIndex, "unknown edge '{}' -> '{}'", source, target);
    removeEdge(edgeId);
}

void RenderGraph::removeEdge(uint32_t edgeId)
{
    CHECK(mpGraph->doesEdgeExist(edgeId), "unknown edge {}", edgeId);
    mpGraph->removeEdge(edgeId);
    mEdgeData.erase(edgeId);
    mRecompile = true;
}

void RenderGraph::setInput(const std::string& name, rhi::IResource* resource)
{
    auto&      inputs  = mCompilerDeps.externalResources;
    const auto current = inputs.find(name);
    if (!resource)
    {
        CHECK(current != inputs.end(), "input '{}' is not registered", name);
        inputs.erase(current);
    }
    else
    {
        if (current == inputs.end())
        {
            std::pair<std::string, std::string> input;
            getRenderPassAndNamePair(true, name, input);
            CHECK(!input.second.empty(), "invalid input '{}'", name);
        }
        inputs.insert_or_assign(name, resource);
    }
    if (mpExe)
        mpExe->setInput(name, resource);
}

void RenderGraph::markOutput(const std::string& name, TextureChannelFlags mask)
{
    CHECK(mask != TextureChannelFlags::None);
    if (name == "*")
    {
        for (const auto& output : getAvailableOutputs())
            markOutput(output, mask);
        return;
    }

    std::pair<std::string, std::string> endpoint;
    getRenderPassAndNamePair(false, name, endpoint);
    CHECK(!endpoint.second.empty(), "invalid render graph output '{}'", name);
    GraphOut   output{getPassIndex(endpoint.first), std::move(endpoint.second), {mask}};
    const auto found = std::ranges::find(mOutputs, output);
    if (found == mOutputs.end())
    {
        mOutputs.push_back(std::move(output));
        mRecompile = true;
    }
    else
        found->masks.insert(mask);
}

void RenderGraph::unmarkOutput(const std::string& name)
{
    const auto [pass, field] = parseFieldName(name);
    const auto node          = getPassIndex(pass);
    CHECK(node != kInvalidIndex && !field.empty(), "invalid render graph output '{}'", name);
    const auto found = std::ranges::find(mOutputs, GraphOut{node, field});
    if (found != mOutputs.end())
    {
        mOutputs.erase(found);
        mRecompile = true;
    }
}

bool RenderGraph::isGraphOutput(const std::string& name) const
{
    const auto [pass, field] = parseFieldName(name);
    return isGraphOutput(GraphOut{getPassIndex(pass), field});
}

bool RenderGraph::isGraphOutput(const GraphOut& output) const
{
    return std::ranges::find(mOutputs, output) != mOutputs.end();
}

rhi::ComPtr<rhi::IResource> RenderGraph::getOutput(const std::string& name)
{
    CHECK(mpExe && !mRecompile, "compile the graph before fetching '{}'", name);
    CHECK(isGraphOutput(name), "'{}' is not a marked graph output", name);
    return mpExe->getResource(name);
}

rhi::ComPtr<rhi::IResource> RenderGraph::getOutput(uint32_t index) { return getOutput(getOutputName(index)); }

std::string RenderGraph::getOutputName(size_t index) const
{

    const auto& output = mOutputs[index];
    return mNodeData.at(output.nodeId).name + '.' + output.field;
}

std::unordered_set<TextureChannelFlags> RenderGraph::getOutputMasks(size_t index) const
{

    return mOutputs[index].masks;
}

std::vector<std::string> RenderGraph::getAvailableOutputs() const
{
    std::vector<std::string> names;
    const auto&              defaults = mCompilerDeps.defaultResourceProps;
    for (uint32_t node = 0; node < mpGraph->getCurrentNodeId(); ++node)
    {
        if (!mpGraph->doesNodeExist(node))
            continue;
        const auto& data       = mNodeData.at(node);
        const auto  reflection = data.pPass->reflect({defaults.dims, defaults.format});
        for (size_t index = 0; index < reflection.getFieldCount(); ++index)
        {
            const auto& field = *reflection.getField(index);
            if (is_set(field.getVisibility(), RenderPassReflection::Field::Visibility::Output))
                names.push_back(data.name + '.' + field.getName());
        }
    }
    return names;
}

void RenderGraph::onResize(Extent2D extent, rhi::Format format)
{
    mCompilerDeps.defaultResourceProps = {{extent.width, extent.height}, format};
    mRecompile                         = true;
}

bool RenderGraph::compile(RenderContext* context, std::string& log)
{

    log.clear();
    if (!mpExe || mRecompile)
    {
        mpExe.reset();

        const logger::ScopedErrorLog errors(log);
        mpExe = RenderGraphCompiler::compile(*this, context, mCompilerDeps);
        if (!mpExe)
            return false;
        mRecompile = false;
    }
    return true;
}

void RenderGraph::execute(RenderContext* context)
{
    const auto& properties = mCompilerDeps.defaultResourceProps;
    mpExe->execute({context, mPassesDictionary, properties.dims});
}

void RenderGraph::renderUI(RenderContext* context, Gui::Widgets& widget)
{
    if (mpExe)
        mpExe->renderUI(context, widget);
}

void RenderGraph::update(const ref<RenderGraph>& source)
{

    std::vector<std::string> removed;
    for (const auto& [passName, index] : mNameToIndex)
        if (!source->doesPassExist(passName))
            removed.push_back(passName);
    for (const auto& name : removed)
        removePass(name);

    for (uint32_t index = 0; index < source->mpGraph->getCurrentNodeId(); ++index)
    {
        if (!source->mpGraph->doesNodeExist(index))
            continue;
        const auto& node = source->mNodeData.at(index);
        if (!doesPassExist(node.name))
            addPass(node.pPass, node.name);
    }

    for (const auto& [edgeId, connection] : mEdgeData)
        mpGraph->removeEdge(edgeId);
    mEdgeData.clear();
    for (uint32_t edgeId = 0; edgeId < source->mpGraph->getCurrentEdgeId(); ++edgeId)
    {
        if (!source->mpGraph->doesEdgeExist(edgeId))
            continue;
        const auto* edge       = source->mpGraph->getEdge(edgeId);
        const auto& connection = source->mEdgeData.at(edgeId);
        const auto& src        = source->mNodeData.at(edge->getSourceNode()).name;
        const auto& dst        = source->mNodeData.at(edge->getDestNode()).name;
        addEdge(
            connection.srcField.empty() ? src : src + '.' + connection.srcField,
            connection.dstField.empty() ? dst : dst + '.' + connection.dstField
        );
    }
    for (size_t index = 0; index < source->getOutputCount(); ++index)
    {
        const auto name = source->getOutputName(index);
        for (const auto mask : source->getOutputMasks(index))
            markOutput(name, mask);
    }
    mRecompile = true;
}

namespace
{
    using ScriptBindings::destroyPythonValue;
    using ScriptBindings::makePythonValue;
    using ScriptBindings::PythonValue;
    using ScriptBindings::valueOf;

    PyObject* createGraph(PyTypeObject* type, PyObject* args, PyObject* keywords)
    {
        const char*              name = nullptr;
        static const char* const kKeywords[]{"name", nullptr};
        if (!PyArg_ParseTupleAndKeywords(args, keywords, "s:RenderGraph", const_cast<char**>(kKeywords), &name))
            return nullptr;
        auto device = getActivePythonRenderGraphDevice();
        if (!device)
        {
            PyErr_SetString(PyExc_RuntimeError, "RenderGraph requires an active device");
            return nullptr;
        }
        return makePythonValue(type, RenderGraph::create(std::move(device), name));
    }

    PyObject* createPass(PyObject* object, PyObject* args, PyObject* keywords)
    {
        const char*              name = nullptr;
        const char*              type = nullptr;
        PyObject*                dict = nullptr;
        static const char* const kKeywords[]{"pass_name", "pass_type", "dict", nullptr};
        if (!PyArg_ParseTupleAndKeywords(
                args,
                keywords,
                "ss|O!:create_pass",
                const_cast<char**>(kKeywords),
                &name,
                &type,
                &PyDict_Type,
                &dict
            ))
            return nullptr;
        const auto properties = dict ? Properties(dict) : Properties();
        if (PyErr_Occurred())
            return nullptr;
        auto pass = valueOf<ref<RenderGraph>>(object)->createPass(name, type, properties);
        return ScriptBindings::toPython(std::move(pass)).release();
    }

    PyObject* updatePass(PyObject* object, PyObject* args, PyObject* keywords)
    {
        const char*              name = nullptr;
        PyObject*                dict = nullptr;
        static const char* const kKeywords[]{"name", "dict", nullptr};
        if (!PyArg_ParseTupleAndKeywords(
                args,
                keywords,
                "sO!:update_pass",
                const_cast<char**>(kKeywords),
                &name,
                &PyDict_Type,
                &dict
            ))
            return nullptr;
        const Properties properties(dict);
        if (PyErr_Occurred())
            return nullptr;
        valueOf<ref<RenderGraph>>(object)->updatePass(name, properties);
        Py_RETURN_NONE;
    }

    PyObject* addEdge(PyObject* object, PyObject* args, PyObject* keywords)
    {
        const char*              source = nullptr;
        const char*              target = nullptr;
        static const char* const kKeywords[]{"src", "dst", nullptr};
        if (!PyArg_ParseTupleAndKeywords(
                args,
                keywords,
                "ss:add_edge",
                const_cast<char**>(kKeywords),
                &source,
                &target
            ))
            return nullptr;
        return PyLong_FromUnsignedLong(valueOf<ref<RenderGraph>>(object)->addEdge(source, target));
    }

    PyObject* removeEdge(PyObject* object, PyObject* args, PyObject* keywords)
    {
        const char*              source = nullptr;
        const char*              target = nullptr;
        static const char* const kKeywords[]{"src", "dst", nullptr};
        if (!PyArg_ParseTupleAndKeywords(
                args,
                keywords,
                "ss:remove_edge",
                const_cast<char**>(kKeywords),
                &source,
                &target
            ))
            return nullptr;
        valueOf<ref<RenderGraph>>(object)->removeEdge(source, target);
        Py_RETURN_NONE;
    }

    PyObject* markOutput(PyObject* object, PyObject* args, PyObject* keywords)
    {
        const char*              name = nullptr;
        PyObject*                mask = nullptr;
        static const char* const kKeywords[]{"name", "mask", nullptr};
        if (!PyArg_ParseTupleAndKeywords(
                args,
                keywords,
                "s|O:mark_output",
                const_cast<char**>(kKeywords),
                &name,
                &mask
            ))
            return nullptr;

        const auto flags = mask ? ScriptBindings::fromPython<TextureChannelFlags>(mask) : TextureChannelFlags::RGB;
        if (!flags)
        {
            if (!PyErr_Occurred())
                PyErr_SetString(PyExc_TypeError, "mark_output mask must be a TextureChannelFlags value");
            return nullptr;
        }
        valueOf<ref<RenderGraph>>(object)->markOutput(name, *flags);
        Py_RETURN_NONE;
    }

    PyObject* getGraphName(PyObject* object, void*)
    {
        const auto& name = valueOf<ref<RenderGraph>>(object)->getName();
        return PyUnicode_FromStringAndSize(name.data(), static_cast<Py_ssize_t>(name.size()));
    }

    int setGraphName(PyObject* object, PyObject* value, void*)
    {
        if (!value || !PyUnicode_Check(value))
        {
            PyErr_SetString(PyExc_TypeError, "RenderGraph.name requires a string");
            return -1;
        }
        const auto* name = PyUnicode_AsUTF8(value);
        if (!name)
            return -1;
        valueOf<ref<RenderGraph>>(object)->setName(name);
        return 0;
    }

    template <const std::string& (RenderPass::*getter)() const>
    PyObject* getPassString(PyObject* object, void*)
    {
        const auto& value = ((*valueOf<ref<RenderPass>>(object)).*getter)();
        return PyUnicode_FromStringAndSize(value.data(), static_cast<Py_ssize_t>(value.size()));
    }

    PyObject* getPassProperties(PyObject* object, void*)
    {
        return valueOf<ref<RenderPass>>(object)->getProperties().toPython();
    }

    PyObject* setPassProperties(PyObject* object, PyObject* dict)
    {
        const Properties properties(dict);
        if (PyErr_Occurred())
            return nullptr;
        valueOf<ref<RenderPass>>(object)->setProperties(properties);
        Py_RETURN_NONE;
    }

}  // namespace

RL_SCRIPT_BINDING(RenderGraph)
{
    RL_SCRIPT_BINDING_DEPENDENCY(RhiInterfaces)

    const auto bind = []<auto Method>() -> PyCFunction
    {
        return reinterpret_cast<PyCFunction>(
            +[](PyObject* object, PyObject* args, PyObject* keywords) -> PyObject*
            {
                const char*              name = nullptr;
                static const char* const kKeywords[]{"name", nullptr};
                if (!PyArg_ParseTupleAndKeywords(args, keywords, "s", const_cast<char**>(kKeywords), &name))
                    return nullptr;

                auto& graph = *valueOf<ref<RenderGraph>>(object);
                if constexpr (std::is_void_v<decltype(std::invoke(Method, graph, name))>)
                {
                    std::invoke(Method, graph, name);
                    Py_RETURN_NONE;
                }
                else
                    return ScriptBindings::toPython(std::invoke(Method, graph, name)).release();
            }
        );
    };
    constexpr auto kGetOutput =
        static_cast<rhi::ComPtr<rhi::IResource> (RenderGraph::*)(const std::string&)>(&RenderGraph::getOutput);

    static PyMethodDef kGraphMethods[]{
        {"create_pass", reinterpret_cast<PyCFunction>(createPass), METH_VARARGS | METH_KEYWORDS},
        {"remove_pass", bind.operator()<&RenderGraph::removePass>(), METH_VARARGS | METH_KEYWORDS},
        {"update_pass", reinterpret_cast<PyCFunction>(updatePass), METH_VARARGS | METH_KEYWORDS},
        {"add_edge", reinterpret_cast<PyCFunction>(addEdge), METH_VARARGS | METH_KEYWORDS},
        {"remove_edge", reinterpret_cast<PyCFunction>(removeEdge), METH_VARARGS | METH_KEYWORDS},
        {"mark_output", reinterpret_cast<PyCFunction>(markOutput), METH_VARARGS | METH_KEYWORDS},
        {"unmark_output", bind.operator()<&RenderGraph::unmarkOutput>(), METH_VARARGS | METH_KEYWORDS},
        {"get_pass", bind.operator()<&RenderGraph::getPass>(), METH_VARARGS | METH_KEYWORDS},
        {"get_output", bind.operator()<kGetOutput>(), METH_VARARGS | METH_KEYWORDS},
        {},
    };
    static PyGetSetDef kGraphProperties[]{
        {"name", getGraphName, setGraphName},
        {},
    };
    static PyGetSetDef kPassProperties[]{
        {"name", getPassString<&RenderPass::getName>},
        {"type", getPassString<&RenderPass::getType>},
        {"desc", getPassString<&RenderPass::getDesc>},
        {"properties", getPassProperties},
        {},
    };
    static PyMethodDef kPassMethods[]{
        {"set_properties", setPassProperties, METH_O},
        {},
    };
    static PyType_Slot kGraphSlots[]{
        {Py_tp_new, reinterpret_cast<void*>(createGraph)},
        {Py_tp_dealloc, reinterpret_cast<void*>(destroyPythonValue<ref<RenderGraph>>)},
        {Py_tp_methods, kGraphMethods},
        {Py_tp_getset, kGraphProperties},
        {Py_mp_subscript,
         reinterpret_cast<void*>(
             +[](PyObject* object, PyObject* key) -> PyObject*
             {
                 const auto* name = PyUnicode_AsUTF8(key);
                 return name ? ScriptBindings::toPython(valueOf<ref<RenderGraph>>(object)->getPass(name)).release()
                             : nullptr;
             }
         )},
        {},
    };
    static PyType_Slot kPassSlots[]{
        {Py_tp_dealloc, reinterpret_cast<void*>(destroyPythonValue<ref<RenderPass>>)},
        {Py_tp_methods, kPassMethods},
        {Py_tp_getset, kPassProperties},
        {},
    };
    static PyType_Spec
        kGraphType{"renderlab.RenderGraph", sizeof(PythonValue<ref<RenderGraph>>), 0, Py_TPFLAGS_DEFAULT, kGraphSlots};
    static PyType_Spec kPassType{
        "renderlab.RenderPass",
        sizeof(PythonValue<ref<RenderPass>>),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
        kPassSlots,
    };
    const auto passType  = ScriptBindings::makePythonType<ref<RenderPass>>(m, kPassType);
    const auto graphType = passType ? ScriptBindings::makePythonType<ref<RenderGraph>>(m, kGraphType) : nullptr;
    if (!graphType || PyModule_AddObjectRef(m, "RenderPass", passType.get()) != 0)
        return -1;
    return PyModule_AddObjectRef(m, "RenderGraph", graphType.get());
}

namespace
{
template <class T>
rhi::ComPtr<T> queryResource(rhi::IResource* resource)
{
    rhi::ComPtr<T> typed;
    if (resource &&
        SLANG_SUCCEEDED(resource->queryInterface(T::getTypeGuid(), reinterpret_cast<void**>(typed.writeRef()))))
        return typed;
    return {};
}
}  // namespace

rhi::ComPtr<rhi::ITexture> RenderGraph::getOutputTexture(const std::string& name)
{
    return queryResource<rhi::ITexture>(getOutput(name));
}
rhi::ComPtr<rhi::ITexture> RenderGraph::getOutputTexture(uint32_t index)
{
    return getOutputTexture(getOutputName(index));
}
rhi::ComPtr<rhi::IBuffer> RenderGraph::getOutputBuffer(const std::string& name)
{
    return queryResource<rhi::IBuffer>(getOutput(name));
}
