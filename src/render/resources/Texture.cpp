#include "render/resources/Texture.h"

#include "core/Logger.h"
#include "core/ScriptBindings.h"
#include "render/device/Device.h"
#include "render/device/Memory.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resources/NativeFormats.h"

#include <utility>

namespace Falcor
{
namespace
{
Resource::Type resourceType(const vk::ImageCreateInfo& info)
{
    if (info.flags & vk::ImageCreateFlagBits::eCubeCompatible)
        return Resource::Type::TextureCube;
    if (info.imageType == vk::ImageType::e1D)
        return Resource::Type::Texture1D;
    if (info.imageType == vk::ImageType::e3D)
        return Resource::Type::Texture3D;
    return info.samples == vk::SampleCountFlagBits::e1 ? Resource::Type::Texture2D : Resource::Type::Texture2DMultisample;
}
}

Texture::Texture(const VulkanContext& vulkan, const vk::ImageCreateInfo& info, ResourceBindFlags bindFlags) :
    Resource(ref<Device>(vulkan.mpDevice), resourceType(info), bindFlags, 0), info(info)
{
    DCHECK(mpDevice);
    const auto& device = vulkan.deviceHandle();
    ownedImage = vkCheck(device.createImage(info));
    const auto requirements = ownedImage.getMemoryRequirements();
    memory = vkCheck(device.allocateMemory({
        .allocationSize = requirements.size,
        .memoryTypeIndex = vulkan_memory::findType(
            vulkan.physicalDeviceHandle(), requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal
        ),
    }));
    vkCheck(ownedImage.bindMemory(*memory, 0));
    image = *ownedImage;
}

Texture::Texture(const VulkanContext& vulkan, vk::Image image, const vk::ImageCreateInfo& info, ResourceBindFlags bindFlags) :
    Resource(ref<Device>(vulkan.mpDevice), resourceType(info), bindFlags, 0), info(info), image(image)
{
    DCHECK(mpDevice && image);
}

Texture::~Texture() { invalidateViews(); }

ref<UnorderedAccessView> Texture::getUAV() { return getUAV(0); }

ref<UnorderedAccessView> Texture::getUAV(uint32_t mipLevel, uint32_t firstArraySlice, uint32_t arraySize)
{
    CHECK(firstArraySlice < getArraySize());
    const auto sliceCount = arraySize == kMaxPossible ? getArraySize() - firstArraySlice : arraySize;

    const ResourceViewInfo viewInfo{mipLevel, 1, firstArraySlice, sliceCount};
    if (const auto view = mUavs.find(viewInfo); view != mUavs.end())
        return view->second;

    auto view = UnorderedAccessView::create(mpDevice.get(), this, mipLevel, firstArraySlice, sliceCount);
    mUavs.emplace(viewInfo, view);
    return view;
}

vk::ImageView Texture::getOrCreateImageView(const vk::ImageViewCreateInfo& viewInfo)
{
    DCHECK(mpDevice && mpDevice->device);
    DCHECK(image);
    CHECK(!viewInfo.pNext);

    const auto&   components = viewInfo.components;
    const auto&   range      = viewInfo.subresourceRange;
    const ViewKey key{
        viewInfo.flags,
        viewInfo.viewType,
        viewInfo.format,
        components.r,
        components.g,
        components.b,
        components.a,
        range.aspectMask,
        range.baseMipLevel,
        range.levelCount,
        range.baseArrayLayer,
        range.layerCount,
    };
    if (const auto found = views.find(key); found != views.end())
        return *found->second;

    vk::ImageViewCreateInfo creationInfo = viewInfo;
    creationInfo.image                   = image;
    auto                view             = vkCheck(mpDevice->device->createImageView(creationInfo));
    const vk::ImageView handle           = *view;
    views.emplace(key, std::move(view));
    return handle;
}

Falcor::ResourceFormat Texture::getFormat() const { return Falcor::getResourceFormat(info.format); }

namespace
{
template <auto getter>
PyObject* getTextureProperty(PyObject* object, void*)
{
    const auto& texture = static_cast<const Texture&>(*Falcor::ScriptBindings::valueOf<ref<Resource>>(object));
    return Falcor::ScriptBindings::toPython((texture.*getter)()).release();
}
}  // namespace

FALCOR_SCRIPT_BINDING(Texture)
{
    using namespace Falcor::ScriptBindings;
    FALCOR_SCRIPT_BINDING_DEPENDENCY(Resource)

    static PyGetSetDef kProperties[]{
        {"format", getTextureProperty<&Texture::getFormat>},
        {"width", getTextureProperty<&Texture::getWidth>},
        {"height", getTextureProperty<&Texture::getHeight>},
        {"depth", getTextureProperty<&Texture::getDepth>},
        {"mip_count", getTextureProperty<&Texture::getMipCount>},
        {"array_size", getTextureProperty<&Texture::getArraySize>},
        {"sample_count", getTextureProperty<&Texture::getSampleCount>},
        {},
    };
    static PyType_Slot kSlots[]{{Py_tp_getset, kProperties}, {}};
    static PyType_Spec kType{
        "falcor.Texture", 0, 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION, kSlots,
    };

    auto* base = getType(typeid(ref<Resource>));
    DCHECK(base);
    const auto texture = makePythonType<ref<Resource>>(m, kType, reinterpret_cast<PyObject*>(base));
    return texture ? PyModule_AddObjectRef(m, "Texture", texture.get()) : -1;
}
}  // namespace Falcor
