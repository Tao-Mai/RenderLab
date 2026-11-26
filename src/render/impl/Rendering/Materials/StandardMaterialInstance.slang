/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#ifndef RENDERLAB_STANDARD_MATERIAL_INSTANCE_SLANG
#define RENDERLAB_STANDARD_MATERIAL_INSTANCE_SLANG

#include "Rendering/Materials/BSDFs/StandardBSDF.slang"
#include "Scene/ShadingFrame.slang"

struct StandardMaterialInstance
{
    StandardBSDFData data;
    ShadingFrame frame;
    float3 emission;
    bool thinSurface;

    float3 eval(float3 viewDirection, float3 direction)
    {
        float3 wi = frame.toLocal(viewDirection);
        StandardBSDF bsdf = StandardBSDF(wi, data, thinSurface);

        return bsdf.eval(wi, frame.toLocal(direction));
    }

    float evalPdf(float3 viewDirection, float3 direction)
    {
        float3 wi = frame.toLocal(viewDirection);
        StandardBSDF bsdf = StandardBSDF(wi, data, thinSurface);

        return bsdf.evalPdf(wi, frame.toLocal(direction));
    }

    BSDFSample sample<S : ISampleGenerator>(float3 viewDirection, inout S generator)
    {
        float3 wi = frame.toLocal(viewDirection);
        StandardBSDF bsdf = StandardBSDF(wi, data, thinSurface);
        BSDFSample result = bsdf.sample(wi, generator);
        result.direction = frame.fromLocal(result.direction);

        return result;
    }
};

#endif
