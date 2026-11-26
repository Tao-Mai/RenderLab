Texture2D<float4> sourceColor;
[format("rgba32f")] RWTexture2D<float4> accumulatedColor;
float sampleWeight;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 size;
    accumulatedColor.GetDimensions(size.x, size.y);
    const uint2 pixel = dispatchId.xy;
    if (any(pixel >= size))
        return;

    float4 color = sourceColor.Load(int3(pixel, 0));
    if (sampleWeight < 1.0)
    {
        const float4 history = accumulatedColor[pixel];
        color = history + (color - history) * sampleWeight;
    }

    accumulatedColor[pixel] = color;
}
