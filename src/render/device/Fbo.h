#pragma once

#include "core/Logger.h"
#include "render/resources/Texture.h"

#include <array>
#include <cstdint>

namespace Falcor
{
class Fbo
{
public:
    void attachColorTarget(const ref<Texture>& texture, uint32_t index)
    {
        DCHECK(index < colorAttachments.size());
        colorAttachments[index] = texture;
    }

    [[nodiscard]] ref<Texture> getColorTexture(uint32_t index) const
    {
        DCHECK(index < colorAttachments.size());
        return colorAttachments[index];
    }

    [[nodiscard]] uint32_t getWidth() const { return colorAttachments[0]->getWidth(); }
    [[nodiscard]] uint32_t getHeight() const { return colorAttachments[0]->getHeight(); }

private:
    static constexpr uint32_t kMaxColorAttachments = 8;
    std::array<ref<Texture>, kMaxColorAttachments> colorAttachments;
};
}  // namespace Falcor
