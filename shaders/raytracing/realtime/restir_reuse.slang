#ifndef RENDERLAB_RESTIR_REUSE_SLANG
#define RENDERLAB_RESTIR_REUSE_SLANG

uint restirIndex(uint2 pixel)
{
    return pixel.y * restirFrame.width + pixel.x;
}

uint restirRandomState(uint2 pixel, uint stageSalt)
{
    static const uint kSampleSeedMultiplier = 0x9e3779b9u;
    return restirIndex(pixel) ^ (parameters.sampleIndex * kSampleSeedMultiplier) ^ stageSalt;
}

bool restirContainsPixel(int2 pixel, bool previous)
{
    if (any(pixel < 0) || pixel.x >= restirFrame.width || pixel.y >= restirFrame.height)
    {
        return false;
    }
    if (!previous)
    {
        return parameters.containsPixel(uint2(pixel));
    }

    float2 center = float2(pixel) + 0.5;
    return all(center >= restirFrame.previousViewportOffset)
        && all(center < restirFrame.previousViewportOffset + restirFrame.previousViewportExtent);
}

bool restirSimilar(RestirSurfaceData center, RestirSurfaceData neighbor, float expectedDepth)
{
    return neighbor.valid && dot(center.geometricNormal, neighbor.geometricNormal) >= kRestirNormalThreshold
        && abs(neighbor.depth - expectedDepth) <= max(kRestirAbsoluteDepthThreshold, kRestirRelativeDepthThreshold * expectedDepth);
}

bool restirReproject(RestirSurfaceData surface, out uint previousIndex)
{
    previousIndex = 0;
    float4 clip = mul(restirFrame.previousViewProjection, float4(surface.position, 1.0));
    if (clip.w <= 0.0)
    {
        return false;
    }
    float3 ndc = clip.xyz / clip.w;
    if (any(abs(ndc.xy) > 1.0) || ndc.z < 0.0 || ndc.z > 1.0)
    {
        return false;
    }
    float depth = dot(restirFrame.previousDepthRow, float4(surface.position, 1.0));
    float2 projected = restirFrame.previousViewportOffset + (ndc.xy * 0.5 + 0.5) * restirFrame.previousViewportExtent;
    int2 nearest = int2(floor(projected));
    float bestError = kMaxRayDistance;
    bool found = false;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            int2 pixel = nearest + int2(x, y);
            if (!restirContainsPixel(pixel, true))
            {
                continue;
            }
            uint index = restirIndex(uint2(pixel));
            RestirSurfaceData previous = restirPreviousSurfaces[index];
            if (!restirSimilar(surface, previous, depth) || previous.objectIndex != surface.objectIndex)
            {
                continue;
            }
            float2 offset = float2(pixel) + 0.5 - projected;
            float error = dot(offset, offset);
            if (error < bestError)
            {
                bestError = error;
                previousIndex = index;
                found = true;
            }
        }
    }

    return found;
}

void restirMerge(inout RestirReservoir result, Surface surface, RestirReservoir source, inout uint state)
{
    float target = source.valid ? restirEvaluate(surface, source.sample).target : 0.0;
    result.stream(source.sample, target, target * source.weightSum * float(source.sampleCount), source.sampleCount, state);
}

float restirSupportedCount(Surface surface, RestirLightSample sample, uint count)
{
    if (count == 0 || sample.kind == kRestirInvalidLight)
    {
        return 0.0;
    }

    RestirLightEvaluation evaluated = restirEvaluate(surface, sample);
    if (evaluated.target <= 0.0 || !surface.isVisible(evaluated.direction, evaluated.distance))
    {
        return 0.0;
    }

    return float(count);
}

[shader("raygeneration")]
void temporalMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint index = restirIndex(pixel);
    RestirSurfaceData current = restirSurfaces[index];
    if (!current.valid || restirFrame.temporalReuse == 0 || restirFrame.historyValid == 0)
    {
        return;
    }

    uint previousIndex;
    if (!restirReproject(current, previousIndex))
    {
        return;
    }

    static const uint kTemporalSeedSalt = 0x8da6b343u;
    uint state = restirRandomState(pixel, kTemporalSeedSalt);
    Surface surface = restirUnpackSurface(current);
    RestirReservoir initial = restirInitialReservoirs[index];
    RestirReservoir previous = restirHistory[(restirFrame.writeLayer ^ 1u) * restirFrame.pixelCount + previousIndex];
    previous.sampleCount = min(previous.sampleCount, initial.sampleCount * restirFrame.maxHistoryLength);

    RestirReservoir combined = {};
    restirMerge(combined, surface, initial, state);
    restirMerge(combined, surface, previous, state);

    // Original ReSTIR, Algorithm 6: count only source domains supporting the selected sample.
    float normalization = restirSupportedCount(surface, combined.sample, initial.sampleCount);
    normalization += restirSupportedCount(restirUnpackSurface(restirPreviousSurfaces[previousIndex]),
        combined.sample, previous.sampleCount);
    combined.finalize(normalization);
    restirDiscardOccluded(surface, combined);
    restirInitialReservoirs[index] = combined;
}

[shader("raygeneration")]
void spatialMain()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint index = restirIndex(pixel);
    RestirSurfaceData center = restirSurfaces[index];
    RestirReservoir result = restirInitialReservoirs[index];
    float3 direct = float3(0.0);
    if (center.valid)
    {
        Surface surface = restirUnpackSurface(center);
        if (restirFrame.spatialReuse != 0)
        {
            static const uint kSpatialSeedSalt = 0xd8163841u;
            uint state = restirRandomState(pixel, kSpatialSeedSalt);
            uint neighbors[kRestirMaxSpatialSamples];
            uint count = 0;
            RestirReservoir combined = {};
            restirMerge(combined, surface, result, state);

            for (uint candidate = 0; candidate < restirFrame.spatialSampleCount; ++candidate)
            {
                float radius = sqrt(randomFloat(state)) * restirFrame.spatialRadius;
                float angle = randomFloat(state) * 2.0 * PI;
                int2 neighborPixel = int2(pixel) + int2(round(radius * float2(cos(angle), sin(angle))));
                if (!restirContainsPixel(neighborPixel, false) || all(neighborPixel == int2(pixel)))
                {
                    continue;
                }
                uint neighborIndex = restirIndex(uint2(neighborPixel));
                RestirSurfaceData neighbor = restirSurfaces[neighborIndex];
                if (!restirSimilar(center, neighbor, center.depth))
                {
                    continue;
                }
                neighbors[count++] = neighborIndex;
                restirMerge(combined, surface, restirInitialReservoirs[neighborIndex], state);
            }

            float normalization = restirSupportedCount(surface, combined.sample, result.sampleCount);
            for (uint neighbor = 0; neighbor < count; ++neighbor)
            {
                uint neighborIndex = neighbors[neighbor];
                normalization += restirSupportedCount(restirUnpackSurface(restirSurfaces[neighborIndex]),
                    combined.sample, restirInitialReservoirs[neighborIndex].sampleCount);
            }
            combined.finalize(normalization);
            result = combined;
        }

        direct = restirShade(surface, result);
    }

    restirHistory[restirFrame.writeLayer * restirFrame.pixelCount + index] = result;
    float4 color = outputImage[pixel];
    outputImage[pixel] = float4(color.rgb + direct, color.a);
}

#endif
