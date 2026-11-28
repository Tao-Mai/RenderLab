#include "Utils/Color/ColorHelpers.slang"
#include "postprocess/exposure_state.slang"

static const uint kExposureHistogramBinCount = 256;
static const float kExposureLogLuminanceMin = -20.0;
static const float kExposureLogLuminanceRange = 40.0;
static const float kExposureCalibration = 1.2;

Texture2D<float4> hdrColor;
RWStructuredBuffer<uint> histogram;
RWStructuredBuffer<ExposureState> exposureState;

groupshared uint localHistogram[kExposureHistogramBinCount];

#if EXPOSURE_STAGE == 0
[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    localHistogram[groupIndex] = 0;
    GroupMemoryBarrierWithGroupSync();

    static const uint kMeteringStride = 2;
    uint2 pixel = thread.xy * kMeteringStride;
    uint width, height;
    hdrColor.GetDimensions(width, height);
    if (all(pixel < uint2(width, height)))
    {
        float luma = luminance(max(hdrColor.Load(int3(pixel, 0)).rgb, float3(0.0)));
        if (luma > 0.0 && isfinite(luma))
        {
            float normalized = saturate((log2(luma) - kExposureLogLuminanceMin) / kExposureLogLuminanceRange);
            uint bin = min(uint(normalized * kExposureHistogramBinCount), kExposureHistogramBinCount - 1);
            InterlockedAdd(localHistogram[bin], 1);
        }
    }

    GroupMemoryBarrierWithGroupSync();
    uint count = localHistogram[groupIndex];
    if (count > 0)
    {
        InterlockedAdd(histogram[groupIndex], count);
    }
}
#else
struct ExposureParameters
{
    float deltaSeconds;
    float manualEv100;
    float exposureCompensation;
    uint automatic;
    float minEv100;
    float maxEv100;
    float brightAdaptationSpeed;
    float darkAdaptationSpeed;
    float lowPercentile;
    float highPercentile;
    uint historyValid;
    uint padding;
};
ConstantBuffer<ExposureParameters> parameters;

[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint groupIndex : SV_GroupIndex)
{
    if (parameters.automatic != 0)
    {
        localHistogram[groupIndex] = histogram[groupIndex];
    }
    else
    {
        localHistogram[groupIndex] = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    if (groupIndex != 0)
    {
        return;
    }

    ExposureState previous = {};
    if (parameters.historyValid != 0)
    {
        previous = exposureState[0];
    }
    bool historyValid = parameters.historyValid != 0 && previous.automaticValid != 0;
    float ev100 = parameters.manualEv100;
    uint automaticValid = 0;
    if (parameters.automatic != 0)
    {
        uint total = 0;
        for (uint bin = 0; bin < kExposureHistogramBinCount; ++bin)
        {
            total += localHistogram[bin];
        }

        ev100 = historyValid ? previous.ev100 : parameters.manualEv100;
        ev100 = clamp(ev100, parameters.minEv100, parameters.maxEv100);
        automaticValid = historyValid ? 1u : 0u;
        if (total > 0)
        {
            float lower = total * parameters.lowPercentile;
            float upper = total * parameters.highPercentile;
            uint cumulative = 0;
            float logSum = 0.0;
            for (uint bin = 0; bin < kExposureHistogramBinCount; ++bin)
            {
                uint count = localHistogram[bin];
                float retained = max(0.0, min(float(cumulative + count), upper) - max(float(cumulative), lower));
                float logLuminance = kExposureLogLuminanceMin + (float(bin) + 0.5) *
                    kExposureLogLuminanceRange / kExposureHistogramBinCount;
                logSum += retained * logLuminance;
                cumulative += count;
            }

            float targetEv100 = clamp(logSum / (upper - lower) + 3.0, parameters.minEv100, parameters.maxEv100);
            float speed = targetEv100 > ev100 ? parameters.brightAdaptationSpeed : parameters.darkAdaptationSpeed;
            float alpha = historyValid ? 1.0 - exp(-speed * parameters.deltaSeconds) : 1.0;
            ev100 = lerp(ev100, targetEv100, alpha);
            automaticValid = 1;
        }
    }

    ExposureState result;
    result.ev100 = ev100;
    result.exposureScale = exp2(parameters.exposureCompensation - ev100) / kExposureCalibration;
    result.automaticValid = automaticValid;
    result.padding = 0;
    exposureState[0] = result;
}
#endif
