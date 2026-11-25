struct AccumulatedPixel
{
    float3 radiance;
    uint sampleCount;
};

Texture2D<float4> inputColor;
[format("rgba16f")] RWTexture2D<float4> outputColor;
RWStructuredBuffer<AccumulatedPixel> historyBuffer;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 size;
    outputColor.GetDimensions(size.x, size.y);
    uint2 pixel = dispatchId.xy;
    if (any(pixel >= size))
        return;

    uint index = pixel.x + pixel.y * size.x;
    AccumulatedPixel history = historyBuffer[index];
    history.radiance += inputColor.Load(int3(pixel, 0)).rgb;
    ++history.sampleCount;
    historyBuffer[index] = history;
    outputColor[pixel] = float4(history.radiance / float(history.sampleCount), 1.0);
}
