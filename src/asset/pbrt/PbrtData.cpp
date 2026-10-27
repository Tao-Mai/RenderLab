#include "asset/pbrt/PbrtData.h"
#include "asset/AssetDataManager.h"
#include "core/Logger.h"

#include <bit>
#include <cstring>
#include <limits>

PbrtParameterValue readPbrtArray(const AssetDataManager& data, const PbrtArrayReference& reference)
{
    static_assert(std::endian::native == std::endian::little);
    const auto read = [&]<class Values>() -> PbrtParameterValue
    {
        using Scalar = typename decltype(Values::values)::value_type;
        CHECK(
            reference.count <= std::numeric_limits<uint64_t>::max() / sizeof(Scalar),
            "invalid PBRT array size: {}",
            reference.path.string()
        );
        const auto bytes = data.readBlob(reference.path, reference.offset, reference.count * sizeof(Scalar));
        Values     result;
        result.values.resize(reference.count);
        std::memcpy(result.values.data(), bytes.data(), bytes.size());
        return result;
    };
    switch (reference.type)
    {
        case PbrtArrayType::Float64:
            return read.template operator()<PbrtFloatValues>();
        case PbrtArrayType::Int32:
            return read.template operator()<PbrtIntegerValues>();
        case PbrtArrayType::Bool8:
            return read.template operator()<PbrtBoolValues>();
    }
    CHECK(false);
}
