#pragma once

#include "asset/pbrt/PbrtTypes.h"

class AssetDataManager;

[[nodiscard]] PbrtParameterValue readPbrtArray(const AssetDataManager& data, const PbrtArrayReference& reference);
