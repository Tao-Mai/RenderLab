#pragma once
#include <slang-rhi.h>
#include <string_view>
#include "core/Logger.h"
#include "core/Metadata.h"
class ShaderLayout
{
public:
    explicit ShaderLayout(rhi::IShaderProgram& program);
    [[nodiscard]] rhi::Format storageImageFormat(std::string_view name) const;
    template <class T>
    void validateConstants(std::string_view name) const
    {
        auto* type = parameter(name).getTypeLayout();
        CHECK(type->getKind() == slang::TypeReflection::Kind::ConstantBuffer, "'{}' must be a constant buffer", name);
        auto* element = type->getElementTypeLayout();
        const auto size = element->getSize();
        CHECK(
            size == sizeof(T),
            "shader parameter '{}' has {} bytes; C++ has {}",
            name,
            size,
            sizeof(T)
        );
        for (const auto& field : metadata::layoutFields(typeid(T)))
        {
            const auto index = element->findFieldIndexByName(field.name);
            CHECK(index >= 0, "shader field '{}.{}' is missing", name, field.name);
            auto* reflected = element->getFieldByIndex(static_cast<unsigned>(index));
            CHECK(
                reflected->getOffset() == field.offset && reflected->getTypeLayout()->getSize() == field.size,
                "shader field '{}.{}' differs from C++",
                name,
                field.name
            );
        }
    }

private:
    [[nodiscard]] slang::VariableLayoutReflection& parameter(std::string_view name) const;

    slang::ProgramLayout* reflection;
};
