#pragma once

#include <slang-rhi.h>
#include "core/Macros.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class RENDERLAB_CORE_API RenderPassReflection
{
public:
    using ResourceDesc = std::variant<rhi::TextureDesc, rhi::BufferDesc>;

    enum class Visibility : uint32_t
    {
        Input    = 1,
        Output   = 2,
        Internal = 4,
    };

    struct Field
    {
        std::string name;
        ResourceDesc resource;
        Visibility visibility;
        bool optional = false;

        [[nodiscard]] const rhi::TextureDesc& texture() const { return std::get<rhi::TextureDesc>(resource); }
    };

    void addInput(std::string name, ResourceDesc resource, bool optional = false);
    void addOutput(std::string name, ResourceDesc resource, bool optional = false);
    void addInputOutput(std::string name, ResourceDesc resource);
    void addInternal(std::string name, ResourceDesc resource);

    [[nodiscard]] std::span<const Field> fields() const { return ports; }
    [[nodiscard]] const Field* getField(std::string_view name) const;

private:
    std::vector<Field> ports;
};

RL_ENUM_CLASS_OPERATORS(RenderPassReflection::Visibility);
