#include "render/resources/Resource.h"

#include "core/Debug.h"
#include "core/Logger.h"
#include "core/ScriptBindings.h"
#include "render/device/Device.h"
#include "render/device/VkCheck.h"
#include "render/resources/Buffer.h"
#include "render/resources/NativeHandleTraits.h"
#include "render/resources/Texture.h"

#include <utility>

namespace Falcor
{
Resource::Resource(ref<Device> device, Type type, ResourceBindFlags bindFlags, uint64_t size) :
    mpDevice(std::move(device)), mType(type), mBindFlags(bindFlags), mSize(size)
{
}

Resource::Resource(Resource&& other) noexcept : Resource({}, other.mType, ResourceBindFlags::None, 0)
{
    *this = std::move(other);
}

Resource& Resource::operator=(Resource&& other) noexcept
{
    DCHECK(other.refCount() == 0);
    invalidateViews();
    other.invalidateViews();
    mpDevice       = std::exchange(other.mpDevice, BreakableReference<Device>(ref<Device>{}));
    mBindFlags     = std::exchange(other.mBindFlags, ResourceBindFlags::None);
    mSize          = std::exchange(other.mSize, 0);
    mName          = std::move(other.mName);
    mType          = other.mType;
    return *this;
}

Resource::~Resource() = default;

ref<Device> Resource::getDevice() const { return mpDevice; }

void Resource::breakStrongReferenceToDevice() { mpDevice.breakStrongReference(); }

NativeHandle Resource::getNativeHandle() const
{
    if (mType == Type::Buffer)
        return NativeHandle(static_cast<VkBuffer>(static_cast<const Buffer*>(this)->handle()));

    return NativeHandle(static_cast<VkImage>(static_cast<const Texture*>(this)->handle()));
}

void Resource::invalidateViews() const
{
    for (const auto& [info, view] : mUavs)
        view->invalidate();
    mUavs.clear();
}

void Resource::setName(const std::string& name)
{
    mName = name;
    DEBUG_EXEC(
        DCHECK(mpDevice && mpDevice->device);
        const auto handle = getNativeHandle();
        const bool isBuffer = mType == Type::Buffer;
        const auto objectHandle = isBuffer ? NativeHandleTrait<VkBuffer>::pack(handle.as<VkBuffer>())
                                           : NativeHandleTrait<VkImage>::pack(handle.as<VkImage>());
        const vk::DebugUtilsObjectNameInfoEXT info{
            .objectType = isBuffer ? vk::ObjectType::eBuffer : vk::ObjectType::eImage,
            .objectHandle = objectHandle,
            .pObjectName = mName.empty() ? nullptr : mName.c_str(),
        };
        vkCheck(mpDevice->device->setDebugUtilsObjectNameEXT(info));
    );
}

ref<Texture> Resource::asTexture() { return refCount() ? ref<Texture>{dynamic_cast<Texture*>(this)} : nullptr; }

ref<Buffer> Resource::asBuffer() { return refCount() ? ref<Buffer>{dynamic_cast<Buffer*>(this)} : nullptr; }

PythonObject ScriptBindings::toPython(ref<Resource> resource)
{
    if (!resource)
        return Falcor::PythonObject{Py_NewRef(Py_None)};

    const char* name = resource->getType() == Resource::Type::Buffer ? "falcor.Buffer" : "falcor.Texture";
    auto*       type = getType(typeid(ref<Resource>), name);
    return Falcor::PythonObject{type ? makePythonValue(type, std::move(resource)) : nullptr};
}

FALCOR_SCRIPT_BINDING(Resource)
{
    using namespace Falcor::ScriptBindings;
    FALCOR_SCRIPT_BINDING_DEPENDENCY(Device)

    static PyGetSetDef kProperties[]{
        {"device", [](PyObject* object, void*) -> PyObject*
        {
            auto device = valueOf<ref<Resource>>(object)->getDevice();
            return device ? toPython(std::move(device)).release() : Py_NewRef(Py_None);
        }},
        {},
    };
    static PyType_Slot kResourceSlots[]{
        {Py_tp_dealloc, reinterpret_cast<void*>(destroyPythonValue<ref<Resource>>)},
        {Py_tp_getset, kProperties},
        {},
    };
    static PyType_Spec kResourceType{
        "falcor.Resource",
        sizeof(PythonValue<ref<Resource>>),
        0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_BASETYPE,
        kResourceSlots,
    };

    const auto resource = makePythonType<ref<Resource>>(m, kResourceType);
    return resource ? PyModule_AddObjectRef(m, "Resource", resource.get()) : -1;
}
}  // namespace Falcor
