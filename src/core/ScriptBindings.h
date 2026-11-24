#pragma once

#include <Python.h>
#include <slang-rhi.h>

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>

struct PythonDeleter
{
    void operator()(PyObject* object) const { Py_DECREF(object); }
};
using PythonObject = std::unique_ptr<PyObject, PythonDeleter>;

[[nodiscard]] std::string pythonError();

namespace ScriptBindings
{
    using RegisterBindingFunc = std::function<int(PyObject* m)>;

    int  initModule(PyObject* m);
    void registerBinding(RegisterBindingFunc bindingFunc);
    void registerDeferredBinding(const std::string& name, RegisterBindingFunc bindingFunc);
    int  resolveDeferredBinding(const std::string& name, PyObject* m);

    namespace detail
    {
        template <class T>
        const void* getNativeInstance(const rhi::ComPtr<T>& value)
        {
            return value.get();
        }

        [[nodiscard]] PyObject* findRegisteredInstance(const void* instance);
        void                    registerInstance(const void* instance, PyObject* object);
        void                    unregisterInstance(const void* instance, PyObject* object);

        template <typename T>
        const void* getNativeInstance(const T&)
        {
            return nullptr;
        }

        template <typename T>
        const void* getNativeInstance(const std::shared_ptr<T>& value)
        {
            return value.get();
        }
    }  // namespace detail

    template <typename T>
    struct PythonValue
    {
        PyObject_HEAD T* value;
    };

    template <typename T>
    const T& valueOf(PyObject* object)
    {
        return *reinterpret_cast<PythonValue<T>*>(object)->value;
    }

    template <typename T>
    PyObject* makePythonValue(PyTypeObject* type, T value)
    {
        const auto* instance = detail::getNativeInstance(value);
        if (instance)
            if (auto* existing = detail::findRegisteredInstance(instance))
                return Py_NewRef(existing);

        auto* object = type->tp_alloc(type, 0);
        if (object)
        {
            reinterpret_cast<PythonValue<T>*>(object)->value = new T(std::move(value));
            if (instance)
                detail::registerInstance(instance, object);
        }
        return object;
    }

    template <typename T>
    void destroyPythonValue(PyObject* object)
    {
        auto* type  = Py_TYPE(object);
        auto* value = reinterpret_cast<PythonValue<T>*>(object)->value;
        if (value)
            if (const auto* instance = detail::getNativeInstance(*value))
                detail::unregisterInstance(instance, object);
        delete value;
        type->tp_free(object);
        Py_DECREF(type);
    }

    int                         registerType(PyObject* type, const std::type_info& nativeType);
    [[nodiscard]] PyTypeObject* getType(const std::type_info& nativeType, const char* name = nullptr);
    [[nodiscard]] bool          isInstance(PyObject* object, const std::type_info& nativeType);

    template <typename T>
    [[nodiscard]] PythonObject makePythonType(PyObject* module, PyType_Spec& spec, PyObject* bases = nullptr)
    {
        PythonObject type{PyType_FromModuleAndSpec(module, &spec, bases)};
        if (type && registerType(type.get(), typeid(T)) != 0)
            return {};
        return type;
    }

    [[nodiscard]] PythonObject toPython(rhi::ComPtr<rhi::IResource> resource);

    template <typename T>
    [[nodiscard]] PythonObject toPython(T&& value)
    {
        using Type = std::decay_t<T>;
        if constexpr (std::is_same_v<Type, PyObject*>)
            return PythonObject{Py_XNewRef(value)};
        else if constexpr (std::is_same_v<Type, bool>)
            return PythonObject{PyBool_FromLong(value)};
        else if constexpr (std::is_integral_v<Type> && std::is_signed_v<Type>)
            return PythonObject{PyLong_FromLongLong(value)};
        else if constexpr (std::is_integral_v<Type>)
            return PythonObject{PyLong_FromUnsignedLongLong(value)};
        else if constexpr (std::is_floating_point_v<Type>)
            return PythonObject{PyFloat_FromDouble(static_cast<double>(value))};
        else if constexpr (std::is_enum_v<Type>)
        {
            auto*      type   = getType(typeid(Type));
            const auto number = type ? toPython(std::to_underlying(value)) : nullptr;
            return PythonObject{
                number ? PyObject_CallOneArg(reinterpret_cast<PyObject*>(type), number.get()) : nullptr
            };
        }
        else if constexpr (std::is_convertible_v<T, std::string_view>)
        {
            const std::string_view text(value);
            return PythonObject{PyUnicode_FromStringAndSize(text.data(), static_cast<Py_ssize_t>(text.size()))};
        }
        else
        {
            auto* type = getType(typeid(Type));
            return PythonObject{type ? makePythonValue(type, std::forward<T>(value)) : nullptr};
        }
    }

    template <typename T>
    [[nodiscard]] std::optional<T> fromPython(PyObject* object)
    {
        if (!object || object == Py_None)
            return std::nullopt;

        if constexpr (std::is_same_v<T, PyObject*>)
            return object;
        else if constexpr (std::is_same_v<T, bool>)
        {
            if (PyBool_Check(object))
                return object == Py_True;
        }
        else if constexpr (std::is_integral_v<T>)
        {
            if (!PyLong_Check(object))
                return std::nullopt;

            const auto number = [object]
            {
                if constexpr (std::is_signed_v<T>)
                    return PyLong_AsLongLong(object);
                else
                    return PyLong_AsUnsignedLongLong(object);
            }();
            if (PyErr_ExceptionMatches(PyExc_OverflowError))
                PyErr_Clear();
            else if (!PyErr_Occurred() && number >= std::numeric_limits<T>::min() &&
                     number <= std::numeric_limits<T>::max())
                return static_cast<T>(number);
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            if (PyFloat_Check(object))
                return static_cast<T>(PyFloat_AS_DOUBLE(object));
            if (PyLong_Check(object))
            {
                const double number = PyLong_AsDouble(object);
                if (!PyErr_Occurred())
                    return static_cast<T>(number);
                if (PyErr_ExceptionMatches(PyExc_OverflowError))
                    PyErr_Clear();
            }
        }
        else if constexpr (std::is_enum_v<T>)
        {
            if (!isInstance(object, typeid(T)))
                return std::nullopt;
            if (const auto value = fromPython<std::underlying_type_t<T>>(object))
                return static_cast<T>(*value);
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            if (!PyUnicode_Check(object))
                return std::nullopt;

            Py_ssize_t  size  = 0;
            const auto* value = PyUnicode_AsUTF8AndSize(object, &size);
            if (value)
                return std::string(value, static_cast<size_t>(size));
        }
        else if (isInstance(object, typeid(T)))
            return valueOf<T>(object);

        return std::nullopt;
    }

    [[nodiscard]] std::string repr(PyObject* value);
    [[nodiscard]] std::string repr(PyObject* value, PyTypeObject* type);

    template <typename T>
    [[nodiscard]] std::string repr(const T& value)
    {
        if constexpr (std::is_enum_v<T>)
            return repr(toPython(std::to_underlying(value)).get(), getType(typeid(T)));
        else
            return repr(toPython(value).get());
    }

}  // namespace ScriptBindings

#define RL_SCRIPT_BINDING(name)                                                                                    \
    static int ScriptBinding##name(PyObject* m);                                                                       \
    namespace                                                                                                          \
    {                                                                                                                  \
        struct ScriptBindingRegisterer##name                                                                           \
        {                                                                                                              \
            ScriptBindingRegisterer##name()                                                                            \
            {                                                                                                          \
                ::ScriptBindings::registerDeferredBinding(#name, ScriptBinding##name);                         \
            }                                                                                                          \
        } gScriptBinding##name;                                                                                        \
    }                                                                                                                  \
    static int ScriptBinding##name(PyObject* m)

#define RL_SCRIPT_BINDING_DEPENDENCY(name)                                                                         \
    if (::ScriptBindings::resolveDeferredBinding(#name, m) != 0)                                               \
        return -1;
