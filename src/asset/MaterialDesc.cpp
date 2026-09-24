#include "asset/AssetDesc.h"

#include "asset/RflReflectors.h"
#include "core/Logger.h"

#include <utility>

#include <rfl/Generic.hpp>
#include <rfl/from_generic.hpp>
#include <rfl/to_generic.hpp>

void applyMaterialFields(MaterialDesc& dst, const MaterialDesc& src)
{
    auto dstObject = rfl::to_generic(dst).to_object();
    CHECK(dstObject, "{}", dstObject.error().what());
    const auto srcObject = rfl::to_generic(src).to_object();
    CHECK(srcObject, "{}", srcObject.error().what());

    for (const auto& [key, value] : *srcObject)
    {
        if (key == "id")
        {
            continue;
        }
        (*dstObject)[key] = value;
    }

    const AssetId id = std::move(dst.id);
    auto parsed = rfl::from_generic<MaterialDesc>(rfl::Generic{std::move(*dstObject)});
    CHECK(parsed, "{}", parsed.error().what());
    dst = std::move(*parsed);
    dst.id = std::move(id);
}
