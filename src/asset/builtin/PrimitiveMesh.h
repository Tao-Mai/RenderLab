#pragma once

#include "asset/MeshAsset.h"
#include "asset/MeshGeometry.h"

// Local Z is the primitive axis; clipping and UVs are defined in that frame.
[[nodiscard]] MeshGeometry generatePrimitiveMesh(const MeshAsset::Primitive& primitive);
