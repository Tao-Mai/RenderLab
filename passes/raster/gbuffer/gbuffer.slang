#include "common/raster/mesh_surface.slang"
#include "common/raster/gbuffer_data.slang"

[shader("fragment")]
GBufferData fragMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace)
{
    return packGBuffer(sampleSurface(input, isFrontFace));
}
