#include "raster/mesh_surface.slang"
#include "raster/gbuffer_data.slang"

[shader("fragment")]
GBufferData fragMain(VertexOutput input, bool isFrontFace : SV_IsFrontFace)
{
    return packGBuffer(sampleSurface(input, isFrontFace));
}
