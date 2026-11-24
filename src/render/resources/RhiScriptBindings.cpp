#include <slang-rhi.h>
#include <array>
#include <ranges>
#include <utility>
#include "core/Logger.h"
#include "core/ScriptBindings.h"
#include "render/device/RhiCheck.h"

namespace ScriptBindings
{
PythonObject toPython(rhi::ComPtr<rhi::IResource> resource)
{
    if (!resource)
        return PythonObject{Py_NewRef(Py_None)};
    rhi::ComPtr<rhi::IBuffer> buffer;
    const auto                isBuffer = SLANG_SUCCEEDED(
        resource->queryInterface(rhi::IBuffer::getTypeGuid(), reinterpret_cast<void**>(buffer.writeRef()))
    );
    auto* type = getType(typeid(rhi::ComPtr<rhi::IResource>), isBuffer ? "renderlab.RhiBuffer" : "renderlab.RhiTexture");
    return PythonObject{type ? makePythonValue(type, std::move(resource)) : nullptr};
}
}  // namespace ScriptBindings

RL_SCRIPT_BINDING(RhiInterfaces)
{
    using namespace ScriptBindings;
    static PyMethodDef kDeviceMethods[]{
        {"create_buffer",
         reinterpret_cast<PyCFunction>(
             +[](PyObject* object, PyObject* args, PyObject* keywords) -> PyObject*
             {
                 static const char* const kKeywords[]{"size", "usage", "memory_type", nullptr};
                 unsigned long long       size        = 0;
                 PyObject*                usageValue  = nullptr;
                 PyObject*                memoryValue = nullptr;
                 if (!PyArg_ParseTupleAndKeywords(
                         args,
                         keywords,
                         "KO|O:create_buffer",
                         const_cast<char**>(kKeywords),
                         &size,
                         &usageValue,
                         &memoryValue
                     ))
                     return nullptr;
                 const auto usage =
                     usageValue ? fromPython<rhi::BufferUsage>(usageValue) : std::optional{rhi::BufferUsage::None};
                 const auto memory = memoryValue ? fromPython<rhi::MemoryType>(memoryValue)
                                                 : std::optional{rhi::MemoryType::DeviceLocal};
                 if (!size || !usage || !memory)
                 {
                     PyErr_SetString(
                         PyExc_ValueError,
                         "create_buffer requires a positive size, BufferUsage and MemoryType"
                     );
                     return nullptr;
                 }
                 rhi::BufferDesc desc;
                 desc.size       = size;
                 desc.usage      = *usage;
                 desc.memoryType = *memory;
                 rhi::ComPtr<rhi::IBuffer> buffer;
                 rhiCheck(valueOf<rhi::ComPtr<rhi::IDevice>>(object)->createBuffer(desc, nullptr, buffer.writeRef()));
                 return toPython(rhi::ComPtr<rhi::IResource>(buffer)).release();
             }
         ),
         METH_VARARGS | METH_KEYWORDS},
        {"wait",
         +[](PyObject* object, PyObject*) -> PyObject*
         {
             const auto queue = valueOf<rhi::ComPtr<rhi::IDevice>>(object)->getQueue(rhi::QueueType::Graphics);
             CHECK(queue);
             rhiCheck(queue->waitOnHost());
             Py_RETURN_NONE;
         },
         METH_NOARGS},
        {}
    };
    static PyType_Slot kDeviceSlots[]{
        {Py_tp_dealloc, reinterpret_cast<void*>(destroyPythonValue<rhi::ComPtr<rhi::IDevice>>)},
        {Py_tp_methods, kDeviceMethods},
        {}
    };
    static PyType_Spec kDeviceType{
        "renderlab.RhiDevice",
        sizeof(PythonValue<rhi::ComPtr<rhi::IDevice>>),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
        kDeviceSlots
    };
    const auto device = makePythonType<rhi::ComPtr<rhi::IDevice>>(m, kDeviceType);
    if (!device || PyModule_AddObjectRef(m, "RhiDevice", device.get()) != 0)
        return -1;

    static PyType_Slot kResourceSlots[]{
        {Py_tp_dealloc, reinterpret_cast<void*>(destroyPythonValue<rhi::ComPtr<rhi::IResource>>)},
        {}
    };
    static PyType_Spec kResourceType{
        "renderlab.RhiResource",
        sizeof(PythonValue<rhi::ComPtr<rhi::IResource>>),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_BASETYPE,
        kResourceSlots
    };
    const auto resource = makePythonType<rhi::ComPtr<rhi::IResource>>(m, kResourceType);
    if (!resource || PyModule_AddObjectRef(m, "RhiResource", resource.get()) != 0)
        return -1;
    static PyGetSetDef kBufferProperties[]{
        {"size",
         +[](PyObject* object, void*) -> PyObject*
         {
             return PyLong_FromUnsignedLongLong(
                 static_cast<rhi::IBuffer*>(valueOf<rhi::ComPtr<rhi::IResource>>(object).get())->getDesc().size
             );
         }},
        {"memory_type",
         +[](PyObject* object, void*) -> PyObject*
         {
             return toPython(
                        static_cast<rhi::IBuffer*>(valueOf<rhi::ComPtr<rhi::IResource>>(object).get())
                            ->getDesc()
                            .memoryType
             )
                 .release();
         }},
        {}
    };
    static PyType_Slot kBufferSlots[]{{Py_tp_getset, kBufferProperties}, {}};
    static PyType_Spec
        kBufferType{"renderlab.RhiBuffer", 0, 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION, kBufferSlots};
    const auto buffer = makePythonType<rhi::ComPtr<rhi::IResource>>(m, kBufferType, resource.get());
    if (!buffer || PyModule_AddObjectRef(m, "RhiBuffer", buffer.get()) != 0)
        return -1;
    static PyGetSetDef kTextureProperties[]{
        {"format",
         +[](PyObject* object, void*) -> PyObject*
         {
             auto* texture = static_cast<rhi::ITexture*>(valueOf<rhi::ComPtr<rhi::IResource>>(object).get());
             return toPython(texture->getDesc().format).release();
         }},
        {"width",
         +[](PyObject* object, void*) -> PyObject*
         {
             return PyLong_FromUnsignedLong(
                 static_cast<rhi::ITexture*>(valueOf<rhi::ComPtr<rhi::IResource>>(object).get())->getDesc().size.width
             );
         }},
        {"height",
         +[](PyObject* object, void*) -> PyObject*
         {
             return PyLong_FromUnsignedLong(
                 static_cast<rhi::ITexture*>(valueOf<rhi::ComPtr<rhi::IResource>>(object).get())->getDesc().size.height
             );
         }},
        {}
    };
    static PyType_Slot kTextureSlots[]{{Py_tp_getset, kTextureProperties}, {}};
    static PyType_Spec
        kTextureType{"renderlab.RhiTexture", 0, 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION, kTextureSlots};
    const auto texture = makePythonType<rhi::ComPtr<rhi::IResource>>(m, kTextureType, resource.get());
    if (!texture || PyModule_AddObjectRef(m, "RhiTexture", texture.get()) != 0)
        return -1;

    const PythonObject enums{PyImport_ImportModule("enum")};
    if (!enums)
        return -1;

    const auto bindEnum = [m, &enums](const char* enumClass, const char* name, const auto& entries)
    {
        using Enum = std::ranges::range_value_t<decltype(entries)>::first_type;
        const PythonObject values{PyDict_New()};
        if (!values)
            return -1;
        for (const auto& [value, label] : entries)
        {
            const auto number = toPython(std::to_underlying(value));
            if (!number || PyDict_SetItemString(values.get(), label, number.get()) != 0)
                return -1;
        }
        const PythonObject type{PyObject_CallMethod(enums.get(), enumClass, "sO", name, values.get())};
        if (!type || registerType(type.get(), typeid(Enum)) != 0)
            return -1;
        return PyModule_AddObjectRef(m, name, type.get());
    };

    const auto formats = std::views::iota(0, std::to_underlying(rhi::Format::_Count)) |
        std::views::transform([](int index)
    {
        const auto format = static_cast<rhi::Format>(index);
        return std::pair{format, rhi::getFormatInfo(format).name};
    });
    constexpr std::array kMemoryTypes{
        std::pair{rhi::MemoryType::DeviceLocal, "DeviceLocal"},
        std::pair{rhi::MemoryType::Upload, "Upload"},
        std::pair{rhi::MemoryType::ReadBack, "ReadBack"},
    };
    constexpr std::array kBufferUsages{
        std::pair{rhi::BufferUsage::None, "None_"},
        std::pair{rhi::BufferUsage::ShaderResource, "ShaderResource"},
        std::pair{rhi::BufferUsage::UnorderedAccess, "UnorderedAccess"},
        std::pair{rhi::BufferUsage::CopySource, "CopySource"},
        std::pair{rhi::BufferUsage::CopyDestination, "CopyDestination"},
        std::pair{rhi::BufferUsage::VertexBuffer, "VertexBuffer"},
        std::pair{rhi::BufferUsage::IndexBuffer, "IndexBuffer"},
        std::pair{rhi::BufferUsage::ConstantBuffer, "ConstantBuffer"},
    };
    if (bindEnum("IntEnum", "Format", formats) != 0 ||
        bindEnum("IntEnum", "MemoryType", kMemoryTypes) != 0 ||
        bindEnum("IntFlag", "BufferUsage", kBufferUsages) != 0)
        return -1;
    return 0;
}
