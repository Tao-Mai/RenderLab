#pragma once

#include <slang-rhi.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class RenderPassReflection
{
public:
    using ResourceDesc = std::variant<rhi::TextureDesc, rhi::BufferDesc>;

    enum class Visibility : uint32_t
    {
        Input    = 1,
        Output   = 2,
        Internal = 4,
    };

    enum class TextureMatch : uint32_t
    {
        None   = 0,
        Extent = 1,
        Format = 2,
    };

    struct Field
    {
        std::string name;
        ResourceDesc resource;
        Visibility visibility;
        bool optional = false;
        // An empty matchInput selects the graph defaults for the requested texture properties.
        std::string matchInput{};
        TextureMatch textureMatch = TextureMatch::None;

        [[nodiscard]] const rhi::TextureDesc& texture() const { return std::get<rhi::TextureDesc>(resource); }
    };

    RenderPassReflection() = default;
    explicit RenderPassReflection(std::span<const Field> fields);

    void addField(Field field);
    void addInput(std::string name, ResourceDesc resource, bool optional = false);
    void addOutput(std::string name, ResourceDesc resource, bool optional = false);
    void addInputOutput(std::string name, ResourceDesc resource);
    void addInternal(std::string name, ResourceDesc resource);
    void applyGraphTextureProperties(rhi::Extent3D extent, rhi::Format format);

    [[nodiscard]] std::span<const Field> fields() const { return ports; }
    [[nodiscard]] const Field* getField(std::string_view name) const;

private:
    std::vector<Field> ports;
};

SLANG_RHI_ENUM_CLASS_OPERATORS(RenderPassReflection::Visibility);
SLANG_RHI_ENUM_CLASS_OPERATORS(RenderPassReflection::TextureMatch);
