#include "render/CopyContext.h"

#include "core/Logger.h"
#include "render/device/Device.h"

namespace Falcor
{
CopyContext::CopyContext(Device* device) : mpDevice(device) { DCHECK(mpDevice); }

CopyContext::~CopyContext() = default;

ref<Device> CopyContext::getDevice() const { return ref<Device>(mpDevice); }
}  // namespace Falcor
