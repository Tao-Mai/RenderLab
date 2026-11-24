#include "render/graph/RenderGraphScript.h"

#include "core/Logger.h"
#include "core/ScriptBindings.h"
#include "core/Scripting.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <type_traits>
#include <utility>

namespace
{
thread_local rhi::ComPtr<rhi::IDevice> activeGraphDevice;

template <class Json = nlohmann::json>
Json jsonFromDict(PyObject* dict)
{
    const PythonObject module{PyImport_ImportModule("json")};
    const PythonObject encoded{module ? PyObject_CallMethod(module.get(), "dumps", "O", dict) : nullptr};
    const auto* source = encoded ? PyUnicode_AsUTF8(encoded.get()) : nullptr;
    if (!source)
        return {};

    auto value = Json::parse(source, nullptr, false);
    if (!value.is_object())
    {
        PyErr_SetString(PyExc_ValueError, "invalid render pass properties");
        return {};
    }
    return value;
}

PyObject* dictFromJson(const auto& value)
{
    const auto encoded = value.dump();
    const PythonObject module{PyImport_ImportModule("json")};
    return module ? PyObject_CallMethod(module.get(), "loads", "s", encoded.c_str()) : nullptr;
}

nlohmann::ordered_json resolveParameters(const nlohmann::ordered_json& defaults, const nlohmann::json& overrides)
{
    auto parameters = defaults;
    for (auto& [name, value] : parameters.items())
    {
        CHECK(value.is_primitive() && !value.is_null(), "unsupported render parameter '{}'", name);
        const auto edited = overrides.find(name);
        if (edited == overrides.end())
            continue;

        if (value.is_number_float())
        {
            CHECK(
                edited->is_number() && std::isfinite(edited->get<double>()),
                "expected a finite number for '{}'",
                name
            );
            value = edited->get<double>();
        }
        else if (value.is_number_integer())
        {
            CHECK(
                edited->is_number_integer() &&
                    (!edited->is_number_unsigned() || std::in_range<int64_t>(edited->get<uint64_t>())),
                "expected a signed 64-bit integer for '{}'",
                name
            );
            value = edited->get<int64_t>();
        }
        else
        {
            CHECK(value.type() == edited->type(), "invalid type for render parameter '{}'", name);
            value = *edited;
        }
    }
    return parameters;
}

std::string factoryName(const std::string& graphName)
{
    auto name = "render_graph_" + graphName;
    std::ranges::replace_if(name, [](char c) { return c == ' ' || c == '/' || c == '\\'; }, '_');
    return name;
}

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
    if (!activeGraphDevice)
    {
        PyErr_SetString(PyExc_RuntimeError, "RenderGraph requires an active device");
        return nullptr;
    }
    return makePythonValue(type, RenderGraph::create(activeGraphDevice, name));
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
    const auto properties = dict ? jsonFromDict(dict) : nlohmann::json::object();
    if (PyErr_Occurred())
        return nullptr;
    auto pass = valueOf<std::shared_ptr<RenderGraph>>(object)->createPass(name, type, properties);
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
    const auto properties = jsonFromDict(dict);
    if (PyErr_Occurred())
        return nullptr;
    valueOf<std::shared_ptr<RenderGraph>>(object)->updatePass(name, properties);
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
    return PyLong_FromUnsignedLong(valueOf<std::shared_ptr<RenderGraph>>(object)->addEdge(source, target));
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
    valueOf<std::shared_ptr<RenderGraph>>(object)->removeEdge(source, target);
    Py_RETURN_NONE;
}

PyObject* getGraphName(PyObject* object, void*)
{
    const auto& name = valueOf<std::shared_ptr<RenderGraph>>(object)->getName();
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
    valueOf<std::shared_ptr<RenderGraph>>(object)->setName(name);
    return 0;
}

template <const std::string& (RenderPass::*getter)() const>
PyObject* getPassString(PyObject* object, void*)
{
    const auto& value = ((*valueOf<std::shared_ptr<RenderPass>>(object)).*getter)();
    return PyUnicode_FromStringAndSize(value.data(), static_cast<Py_ssize_t>(value.size()));
}

PyObject* getPassProperties(PyObject* object, void*)
{
    return dictFromJson(valueOf<std::shared_ptr<RenderPass>>(object)->getProperties());
}

PyObject* setPassProperties(PyObject* object, PyObject* dict)
{
    if (!PyDict_Check(dict))
    {
        PyErr_SetString(PyExc_TypeError, "properties must be a dict");
        return nullptr;
    }

    const auto properties = jsonFromDict(dict);
    if (PyErr_Occurred())
        return nullptr;
    valueOf<std::shared_ptr<RenderPass>>(object)->setProperties(properties);
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

                auto& graph = *valueOf<std::shared_ptr<RenderGraph>>(object);
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
        {"mark_output", bind.operator()<&RenderGraph::markOutput>(), METH_VARARGS | METH_KEYWORDS},
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
        {Py_tp_dealloc, reinterpret_cast<void*>(destroyPythonValue<std::shared_ptr<RenderGraph>>)},
        {Py_tp_methods, kGraphMethods},
        {Py_tp_getset, kGraphProperties},
        {Py_mp_subscript,
         reinterpret_cast<void*>(
             +[](PyObject* object, PyObject* key) -> PyObject*
             {
                 const auto* name = PyUnicode_AsUTF8(key);
                 if (!name)
                     return nullptr;

                 const auto& graph = valueOf<std::shared_ptr<RenderGraph>>(object);
                 return ScriptBindings::toPython(graph->getPass(name)).release();
             }
         )},
        {},
    };
    static PyType_Slot kPassSlots[]{
        {Py_tp_dealloc, reinterpret_cast<void*>(destroyPythonValue<std::shared_ptr<RenderPass>>)},
        {Py_tp_methods, kPassMethods},
        {Py_tp_getset, kPassProperties},
        {},
    };
    static PyType_Spec kGraphType{
        "renderlab.RenderGraph",
        sizeof(PythonValue<std::shared_ptr<RenderGraph>>),
        0,
        Py_TPFLAGS_DEFAULT,
        kGraphSlots,
    };
    static PyType_Spec kPassType{
        "renderlab.RenderPass",
        sizeof(PythonValue<std::shared_ptr<RenderPass>>),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
        kPassSlots,
    };
    const auto passType = ScriptBindings::makePythonType<std::shared_ptr<RenderPass>>(m, kPassType);
    const auto graphType =
        passType ? ScriptBindings::makePythonType<std::shared_ptr<RenderGraph>>(m, kGraphType) : nullptr;
    if (!graphType || PyModule_AddObjectRef(m, "RenderPass", passType.get()) != 0)
        return -1;
    return PyModule_AddObjectRef(m, "RenderGraph", graphType.get());
}

LoadedRenderGraph loadRenderGraph(
    rhi::IDevice& device, const std::filesystem::path& path, const std::string& graphName,
    const nlohmann::json& overrides
)
{
    Scripting::Context script;
    script.setObject("__name__", std::string("RenderLab"));

    auto previousDevice = std::exchange(activeGraphDevice, rhi::ComPtr<rhi::IDevice>{&device});
    Scripting::runScriptFromFile(path, script);

    const auto functionName = factoryName(graphName);
    auto* factory = script.getObject<PyObject*>(functionName);
    CHECK(PyFunction_Check(factory), "graph '{}' must define '{}'", graphName, functionName);

    LoadedRenderGraph loaded;
    if (auto* defaults = PyFunction_GetKwDefaults(factory))
    {
        loaded.defaults = jsonFromDict<nlohmann::ordered_json>(defaults);
        CHECK(!PyErr_Occurred(), "failed to read parameters of '{}': {}", graphName, pythonError());
    }
    loaded.parameters = resolveParameters(loaded.defaults, overrides);

    const PythonObject arguments{PyTuple_New(0)};
    const PythonObject keywords{dictFromJson(loaded.parameters)};
    CHECK(arguments && keywords, "{}", pythonError());
    const PythonObject result{PyObject_Call(factory, arguments.get(), keywords.get())};
    CHECK(result, "failed to build graph '{}': {}", graphName, pythonError());
    const auto graph = ScriptBindings::fromPython<std::shared_ptr<RenderGraph>>(result.get());
    CHECK(graph && *graph, "graph function '{}' must return a RenderGraph", functionName);
    CHECK((*graph)->getName() == graphName, "'{}' returned graph '{}' while '{}' was configured",
        functionName, (*graph)->getName(), graphName);
    CHECK((*graph)->getOutputCount(), "graph '{}' requires a marked output", graphName);

    loaded.graph = *graph;
    loaded.graph->captureScriptedProperties();
    activeGraphDevice = std::move(previousDevice);
    return loaded;
}
