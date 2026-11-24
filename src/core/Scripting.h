#pragma once

#include "core/Macros.h"
#include "core/Logger.h"
#include "core/ScriptBindings.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

class RENDERLAB_CORE_API Scripting
{
public:
    class Context
    {
    public:
        explicit Context(PyObject* globals);
        Context();

        template <typename T>
        void setObject(const std::string& name, T object)
        {
            const auto value = ScriptBindings::toPython(std::move(object));
            CHECK(value && PyDict_SetItemString(mGlobals.get(), name.c_str(), value.get()) == 0, "{}", pythonError());
        }

        template <typename T>
        [[nodiscard]] T getObject(const std::string& name) const
        {
            auto value = ScriptBindings::fromPython<T>(PyDict_GetItemString(mGlobals.get(), name.c_str()));
            CHECK(!PyErr_Occurred(), "cannot read script object '{}': {}", name, pythonError());
            CHECK(value, "script object '{}' is missing or has the wrong type", name);
            return std::move(*value);
        }

        [[nodiscard]] bool containsObject(const std::string& name) const
        {
            return PyDict_GetItemString(mGlobals.get(), name.c_str()) != nullptr;
        }

    private:
        friend class Scripting;
        std::unique_ptr<PyObject, void (*)(PyObject*)> mGlobals;
    };

    static void     start();
    static void     shutdown();
    static bool     isRunning() { return Py_IsInitialized() != 0; }
    static Context& getDefaultContext();
    static Context  getCurrentContext();

    struct RunResult
    {
        std::string out;
        std::string err;
    };

    static RunResult
    runScript(std::string_view script, Context& context = getDefaultContext(), bool captureOutput = false);
    static RunResult runScriptFromFile(
        const std::filesystem::path& path, Context& context = getDefaultContext(), bool captureOutput = false
    );
    static std::string interpretScript(const std::string& script, Context& context = getDefaultContext());

private:
    static std::unique_ptr<Context> sDefaultContext;
};
