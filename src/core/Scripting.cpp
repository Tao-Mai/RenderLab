#include "core/Scripting.h"

#include "core/Logger.h"

#include <cstdlib>

extern "C" PyObject* PyInit_falcor();

namespace Falcor
{
const FileDialogFilterVec           Scripting::kFileExtensionFilters{{"py", "Script Files"}};
std::unique_ptr<Scripting::Context> Scripting::sDefaultContext;

namespace
{
    PythonObject pythonPath(const std::filesystem::path& path)
    {
#ifdef _WIN32
        return PythonObject{PyUnicode_FromWideChar(path.c_str(), static_cast<Py_ssize_t>(path.native().size()))};
#else
        return PythonObject{PyUnicode_DecodeFSDefault(path.c_str())};
#endif
    }

    class RedirectStream
    {
    public:
        explicit RedirectStream(const char* stream) : mStream(stream), mOrigStream(Py_XNewRef(PySys_GetObject(stream)))
        {
            const PythonObject io{PyImport_ImportModule("io")};
            CHECK(io, "{}", pythonError());
            mBuffer.reset(PyObject_CallMethod(io.get(), "StringIO", nullptr));
            CHECK(mBuffer && PySys_SetObject(mStream, mBuffer.get()) == 0, "{}", pythonError());
        }

        ~RedirectStream() { CHECK(PySys_SetObject(mStream, mOrigStream.get()) == 0, "{}", pythonError()); }

        operator std::string() const
        {
            const PythonObject value{PyObject_CallMethod(mBuffer.get(), "getvalue", nullptr)};
            auto               text = ScriptBindings::fromPython<std::string>(value.get());
            CHECK(text, "{}", pythonError());
            return std::move(*text);
        }

    private:
        const char*  mStream;
        PythonObject mOrigStream;
        PythonObject mBuffer;
    };

    template <typename Run>
    Scripting::RunResult runScript(Run run, bool captureOutput)
    {
        if (!captureOutput)
        {
            run();
            return {};
        }

        const RedirectStream out("stdout");
        const RedirectStream err("stderr");
        run();
        return {out, err};
    }
}  // namespace

std::string pythonError()
{
    PyObject* type      = nullptr;
    PyObject* value     = nullptr;
    PyObject* traceback = nullptr;
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);
    const PythonObject errorType{type};
    const PythonObject errorValue{value};
    const PythonObject errorTraceback{traceback};
    const PythonObject formatter{PyImport_ImportModule("traceback")};
    const PythonObject lines{
        formatter && type ? PyObject_CallMethod(
                                formatter.get(),
                                "format_exception",
                                "OOO",
                                type,
                                value,
                                traceback ? traceback : Py_None
                            )
                          : nullptr
    };
    const PythonObject separator{PyUnicode_FromString("")};
    const PythonObject message{
        lines       ? PyUnicode_Join(separator.get(), lines.get())
            : value ? PyObject_Str(value)
                    : nullptr
    };
    const char* text   = message ? PyUnicode_AsUTF8(message.get()) : nullptr;
    const auto  result = text ? std::string{text} : std::string{"unknown Python error"};
    PyErr_Clear();
    return result;
}

Scripting::Context::Context(PyObject* globals) : mGlobals(Py_NewRef(globals), Py_DecRef) {}

Scripting::Context::Context() :
    mGlobals(
        nullptr,
        [](PyObject* globals)
        {
            // Release references from script functions back to their globals before the device is destroyed.
            PyDict_Clear(globals);
            Py_DECREF(globals);
        }
    )
{
    Scripting::start();
    mGlobals.reset(PyDict_New());
    CHECK(mGlobals);
    setObject("__builtins__", PyEval_GetBuiltins());
}

void Scripting::start()
{
    if (isRunning())
        return;

    CHECK(PyImport_AppendInittab("falcor", PyInit_falcor) == 0);
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.write_bytecode = 0;

    const auto home = PyConfig_SetString(&config, &config.home, RENDERLAB_PYTHON_HOME);
    CHECK(!PyStatus_Exception(home), "failed to configure Python: {}", home.err_msg);
    const auto status = Py_InitializeFromConfig(&config);
    CHECK(!PyStatus_Exception(status), "failed to initialize Python: {}", status.err_msg);
    PyConfig_Clear(&config);

    static const int kRegistered = std::atexit(shutdown);
    CHECK(kRegistered == 0);
    sDefaultContext = std::make_unique<Context>();
    runScript("from falcor import *");
}

void Scripting::shutdown()
{
    if (isRunning())
    {
        sDefaultContext.reset();
        const auto status = Py_FinalizeEx();
        CHECK(status == 0, "failed to finalize Python");
    }
}

Scripting::Context& Scripting::getDefaultContext()
{
    start();
    return *sDefaultContext;
}

Scripting::Context Scripting::getCurrentContext()
{
    start();
    if (auto* globals = PyEval_GetGlobals())
        return Context(globals);

    auto* main = PyImport_AddModule("__main__");
    CHECK(main, "{}", pythonError());
    return Context(PyModule_GetDict(main));
}

Scripting::RunResult Scripting::runScript(std::string_view script, Context& context, bool captureOutput)
{
    const std::string source{script};
    return Falcor::runScript(
        [&]
        {
            auto*              globals = context.mGlobals.get();
            const PythonObject result{PyRun_String(source.c_str(), Py_file_input, globals, globals)};
            CHECK(result, "{}", pythonError());
        },
        captureOutput
    );
}

Scripting::RunResult
Scripting::runScriptFromFile(const std::filesystem::path& path, Context& context, bool captureOutput)
{
    const auto         filename  = pythonPath(path);
    const auto         directory = pythonPath(path.parent_path());
    const PythonObject searchPath{Py_XNewRef(PySys_GetObject("path"))};
    CHECK(searchPath && PyList_Check(searchPath.get()), "Python sys.path must be a list");
    const PythonObject previousPath{PyList_GetSlice(searchPath.get(), 0, PyList_GET_SIZE(searchPath.get()))};
    CHECK(filename && directory && previousPath, "{}", pythonError());

    const PythonObject builtins{PyImport_ImportModule("builtins")};
    CHECK(builtins, "{}", pythonError());
    const PythonObject file{PyObject_CallMethod(builtins.get(), "open", "Os", filename.get(), "rb")};
    CHECK(file, "{}", pythonError());
    const PythonObject source{PyObject_CallMethod(file.get(), "read", nullptr)};
    CHECK(source, "{}", pythonError());
    if (!context.containsObject("__name__"))
        context.setObject("__name__", std::string("__main__"));
    context.setObject("__file__", filename.get());

    CHECK(PyList_Insert(searchPath.get(), 0, directory.get()) == 0, "{}", pythonError());
    auto output = Falcor::runScript(
        [&]
        {
            const PythonObject code{
                PyObject_CallMethod(builtins.get(), "compile", "OOs", source.get(), filename.get(), "exec")
            };
            CHECK(code, "failed to compile '{}': {}", path.string(), pythonError());
            auto*              globals = context.mGlobals.get();
            const PythonObject result{PyEval_EvalCode(code.get(), globals, globals)};
            CHECK(result, "failed to run '{}': {}", path.string(), pythonError());
        },
        captureOutput
    );

    CHECK(
        PyList_SetSlice(searchPath.get(), 0, PyList_GET_SIZE(searchPath.get()), previousPath.get()) == 0 &&
            PySys_SetObject("path", searchPath.get()) == 0,
        "{}",
        pythonError()
    );
    return output;
}

std::string Scripting::interpretScript(const std::string& script, Context& context)
{
    const PythonObject code{PyImport_ImportModule("code")};
    CHECK(code, "{}", pythonError());
    const PythonObject interpreter{
        PyObject_CallMethod(code.get(), "InteractiveInterpreter", "O", context.mGlobals.get())
    };
    CHECK(interpreter, "{}", pythonError());

    auto output = Falcor::runScript(
        [&]
        {
            const auto source = ScriptBindings::toPython(script);
            CHECK(source, "{}", pythonError());
            const PythonObject result{PyObject_CallMethod(interpreter.get(), "runsource", "O", source.get())};
            CHECK(result, "{}", pythonError());
        },
        true
    );
    output.out += output.err;
    return std::move(output.out);
}
}  // namespace Falcor
