#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "render/graph/PythonGraph.h"

#include "core/Logger.h"
#include "core/Serializer.h"
#include "render/RendererConfig.h"

#include <memory>
#include <string>

namespace
{
struct PythonDeleter
{
    void operator()(PyObject* object) const { Py_DECREF(object); }
};
using PythonObject = std::unique_ptr<PyObject, PythonDeleter>;

std::string pythonError()
{
    PyObject* type = nullptr;
    PyObject* value = nullptr;
    PyObject* traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    const PythonObject errorType{type};
    const PythonObject errorValue{value};
    const PythonObject errorTraceback{traceback};
    auto* detail = value ? value : type;
    const PythonObject message{detail ? PyObject_Str(detail) : nullptr};
    const char* text = message ? PyUnicode_AsUTF8(message.get()) : nullptr;
    const std::string result = text ? text : "Python reported no error details";
    PyErr_Clear();
    return result;
}

PythonObject pythonPath(const std::filesystem::path& path)
{
#ifdef _WIN32
    return PythonObject{PyUnicode_FromWideChar(path.c_str(), static_cast<Py_ssize_t>(path.native().size()))};
#else
    return PythonObject{PyUnicode_DecodeFSDefault(path.c_str())};
#endif
}

class PythonRuntime
{
public:
    PythonRuntime()
    {
        PyConfig config;
        PyConfig_InitIsolatedConfig(&config);
        config.write_bytecode = 0;

        const auto home = PyConfig_SetString(&config, &config.home, RENDERLAB_PYTHON_HOME);
        CHECK(!PyStatus_Exception(home), "failed to configure Python: {}", home.err_msg);
        const auto status = Py_InitializeFromConfig(&config);
        CHECK(!PyStatus_Exception(status), "failed to initialize Python: {}", status.err_msg);
        PyConfig_Clear(&config);

        const auto directory = pythonPath(RENDERLAB_GRAPH_LIBRARY_DIRECTORY);
        CHECK(directory, "invalid Python graph library path: {}", pythonError());
        const auto result = PyList_Insert(PySys_GetObject("path"), 0, directory.get());
        CHECK(result == 0, "failed to add Python graph library path: {}", pythonError());
    }

    ~PythonRuntime() { Py_FinalizeEx(); }
    PythonRuntime(const PythonRuntime&) = delete;
    PythonRuntime& operator=(const PythonRuntime&) = delete;
};
}

nlohmann::json loadRenderGraph(const std::filesystem::path& script, const RendererConfig& config)
{
    static const PythonRuntime kRuntime;

    const PythonObject module{PyImport_ImportModule("renderlab_graph")};
    CHECK(module, "failed to import the graph builder: {}", pythonError());
    const PythonObject build{PyObject_GetAttrString(module.get(), "build_description")};
    CHECK(build, "failed to find the Python graph builder: {}", pythonError());

    const auto configJson = Serialize(config).dump();
    const PythonObject pythonConfig{PyUnicode_FromStringAndSize(configJson.data(), static_cast<Py_ssize_t>(configJson.size()))};
    const auto path = pythonPath(script);
    CHECK(path && pythonConfig, "failed to prepare render graph '{}': {}", script.string(), pythonError());
    const PythonObject result{PyObject_CallFunctionObjArgs(build.get(), path.get(), pythonConfig.get(), nullptr)};
    CHECK(result, "failed to build render graph '{}': {}", script.string(), pythonError());

    const char* description = PyUnicode_AsUTF8(result.get());
    CHECK(description, "invalid result for render graph '{}': {}", script.string(), pythonError());
    auto graph = nlohmann::json::parse(description, nullptr, false);
    CHECK(graph.is_object(), "invalid description for render graph '{}'", script.string());
    return graph;
}
