#pragma once

struct GltfMaterial;

// Merge src into dst in place: only std::optional fields with values overwrite.
void applyOptionalFields(GltfMaterial& dst, const GltfMaterial& src);
