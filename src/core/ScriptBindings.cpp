#include "core/ScriptBindings.h"

#include "core/Logger.h"

#include <algorithm>
#include <list>
#include <unordered_map>

namespace Falcor::ScriptBindings
{
namespace
{
    constexpr char kTypeRegistry[]     = "RenderLab.ScriptBindings";
    constexpr char kModuleKey[]        = "RenderLab.ScriptBindings.module";
    constexpr char kInstanceRegistry[] = "RenderLab.ScriptBindings.instances";

    using Instances = std::unordered_map<const void*, PyObject*>;

    struct DeferredBinding
    {
        std::string         name;
        RegisterBindingFunc bindingFunc;
        bool                isRegistered = false;

        int bind(PyObject* m)
        {
            if (isRegistered)
                return 0;

            isRegistered      = true;
            const auto result = bindingFunc(m);
            isRegistered      = result == 0;
            return result;
        }
    };

    std::list<DeferredBinding>& getDeferredBindings()
    {
        static std::list<DeferredBinding> deferredBindings;
        return deferredBindings;
    }

    std::string getUniqueDeferredBindingName()
    {
        static uint32_t id = 0;
        return "_DeferredBinding" + std::to_string(id++);
    }

    PyObject* getRegisteredTypes(const std::type_info& nativeType)
    {
        auto* state    = PyInterpreterState_GetDict(PyInterpreterState_Get());
        auto* registry = state ? PyDict_GetItemString(state, kTypeRegistry) : nullptr;
        return registry ? PyDict_GetItemString(registry, nativeType.name()) : nullptr;
    }

    Instances* getRegisteredInstances()
    {
        auto* state    = PyInterpreterState_GetDict(PyInterpreterState_Get());
        auto* registry = state ? PyDict_GetItemString(state, kInstanceRegistry) : nullptr;
        return registry ? static_cast<Instances*>(PyCapsule_GetPointer(registry, kInstanceRegistry)) : nullptr;
    }
}  // namespace

PyObject* detail::findRegisteredInstance(const void* instance)
{
    const auto* instances = getRegisteredInstances();

    const auto found = instances->find(instance);
    return found != instances->end() ? found->second : nullptr;
}

void detail::registerInstance(const void* instance, PyObject* object)
{
    auto* instances = getRegisteredInstances();

    instances->emplace(instance, object);
}

void detail::unregisterInstance(const void* instance, PyObject* object)
{
    // The interpreter can release its registry before destroying the remaining wrappers.
    if (auto* instances = getRegisteredInstances())
    {
        const auto found = instances->find(instance);

        instances->erase(found);
    }
}

int initModule(PyObject* m)
{
    auto* state = PyInterpreterState_GetDict(PyInterpreterState_Get());
    if (!state)
    {
        PyErr_NoMemory();
        return -1;
    }

    const PythonObject types{PyDict_New()};
    auto               registeredInstances = std::make_unique<Instances>();
    const PythonObject instances{PyCapsule_New(
        registeredInstances.get(),
        kInstanceRegistry,
        [](PyObject* capsule) { delete static_cast<Instances*>(PyCapsule_GetPointer(capsule, kInstanceRegistry)); }
    )};
    if (instances)
        registeredInstances.release();
    if (!types || !instances || PyDict_SetItemString(state, kTypeRegistry, types.get()) != 0 ||
        PyDict_SetItemString(state, kInstanceRegistry, instances.get()) != 0)
        return -1;

    auto& bindings = getDeferredBindings();
    for (auto& binding : bindings)
        binding.isRegistered = false;

    if (std::ranges::all_of(bindings, [m](auto& binding) { return binding.bind(m) == 0; }) &&
        PyDict_SetItemString(state, kModuleKey, m) == 0)
        return 0;

    PyDict_Clear(types.get());
    return -1;
}

void registerBinding(RegisterBindingFunc bindingFunc)
{
    if (Py_IsInitialized())
    {
        auto* state = PyInterpreterState_GetDict(PyInterpreterState_Get());
        if (auto* m = state ? PyDict_GetItemString(state, kModuleKey) : nullptr)
        {
            CHECK(bindingFunc(m) == 0, "{}", pythonError());
            return;
        }
    }

    registerDeferredBinding(getUniqueDeferredBindingName(), std::move(bindingFunc));
}

void registerDeferredBinding(const std::string& name, RegisterBindingFunc bindingFunc)
{
    auto& bindings = getDeferredBindings();
    CHECK(
        std::ranges::find(bindings, name, &DeferredBinding::name) == bindings.end(),
        "script binding '{}' is already registered",
        name
    );
    bindings.emplace_back(name, std::move(bindingFunc));
}

int resolveDeferredBinding(const std::string& name, PyObject* m)
{
    auto&      bindings = getDeferredBindings();
    const auto binding  = std::ranges::find(bindings, name, &DeferredBinding::name);
    if (binding == bindings.end())
    {
        PyErr_Format(PyExc_ImportError, "script binding '%s' is missing", name.c_str());
        return -1;
    }
    return binding->bind(m);
}

int registerType(PyObject* type, const std::type_info& nativeType)
{

    const char* name  = reinterpret_cast<PyTypeObject*>(type)->tp_name;
    auto*       state = PyInterpreterState_GetDict(PyInterpreterState_Get());

    auto* registry = PyDict_GetItemString(state, kTypeRegistry);

    if (auto* types = PyDict_GetItemString(registry, nativeType.name()))
        return PyDict_SetItemString(types, name, type);

    const PythonObject types{PyDict_New()};
    if (!types || PyDict_SetItemString(types.get(), name, type) != 0)
        return -1;
    return PyDict_SetItemString(registry, nativeType.name(), types.get());
}

PyTypeObject* getType(const std::type_info& nativeType, const char* name)
{
    if (auto* types = getRegisteredTypes(nativeType))
    {
        PyObject* type = nullptr;
        if (name)
            type = PyDict_GetItemString(types, name);
        else
        {
            // The primary binding is registered before its derived types.
            Py_ssize_t index = 0;
            PyDict_Next(types, &index, nullptr, &type);
        }
        if (type)
            return reinterpret_cast<PyTypeObject*>(type);
    }

    PyErr_Format(PyExc_TypeError, "no Python binding for '%s'", name ? name : nativeType.name());
    return nullptr;
}

bool isInstance(PyObject* object, const std::type_info& nativeType)
{
    auto* types = getRegisteredTypes(nativeType);
    if (!types)
        return false;

    const auto* type    = reinterpret_cast<PyObject*>(Py_TYPE(object));
    Py_ssize_t  index   = 0;
    PyObject*   binding = nullptr;
    while (PyDict_Next(types, &index, nullptr, &binding))
        if (binding == type)
            return true;
    return false;
}

std::string repr(PyObject* value)
{
    CHECK(value, "{}", pythonError());
    const PythonObject encoded{PyObject_Repr(value)};
    auto               text = fromPython<std::string>(encoded.get());
    CHECK(text, "{}", pythonError());
    return std::move(*text);
}

std::string repr(PyObject* value, PyTypeObject* type)
{
    CHECK(value && type, "{}", pythonError());
    const PythonObject name{PyObject_GetAttrString(reinterpret_cast<PyObject*>(type), "__name__")};
    const auto         typeName = fromPython<std::string>(name.get());
    CHECK(typeName, "{}", pythonError());
    return *typeName + '(' + repr(value) + ')';
}
}  // namespace Falcor::ScriptBindings

extern "C" PyObject* PyInit_falcor()
{
    static PyModuleDef   kModule{PyModuleDef_HEAD_INIT, "falcor", nullptr, -1};
    Falcor::PythonObject module{PyModule_Create(&kModule)};
    if (!module || Falcor::ScriptBindings::initModule(module.get()) != 0)
        return nullptr;
    return module.release();
}
