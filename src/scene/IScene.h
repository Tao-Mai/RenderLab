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
#pragma once

#include "core/Macros.h"
#include "core/Object.h"

namespace Falcor
{
class IScene : public Object
{
    FALCOR_OBJECT(IScene)

public:
    enum class UpdateFlags
    {
        None                        = 0x0,
        GeometryMoved               = 0x1,
        CameraMoved                 = 0x2,
        CameraPropertiesChanged     = 0x4,
        CameraSwitched              = 0x8,
        LightsMoved                 = 0x10,
        LightIntensityChanged       = 0x20,
        LightPropertiesChanged      = 0x40,
        SceneGraphChanged           = 0x80,
        LightCollectionChanged      = 0x100,
        MaterialsChanged            = 0x200,
        EnvMapChanged               = 0x400,
        EnvMapPropertiesChanged     = 0x800,
        LightCountChanged           = 0x1000,
        RenderSettingsChanged       = 0x2000,
        GridVolumesMoved            = 0x4000,
        GridVolumePropertiesChanged = 0x8000,
        GridVolumeGridsChanged      = 0x10000,
        GridVolumeBoundsChanged     = 0x20000,
        CurvesMoved                 = 0x40000,
        CustomPrimitivesMoved       = 0x80000,
        GeometryChanged             = 0x100000,
        DisplacementChanged         = 0x200000,
        SDFGridConfigChanged        = 0x400000,
        SDFGeometryChanged          = 0x800000,
        MeshesChanged               = 0x1000000,
        SceneDefinesChanged         = 0x2000000,
        TypeConformancesChanged     = 0x4000000,
        ShaderCodeChanged           = 0x8000000,
        EmissiveMaterialsChanged    = 0x10000000,
        RecompileNeeded             = SceneDefinesChanged | TypeConformancesChanged | ShaderCodeChanged,
        All                         = -1,
    };

    ~IScene() override = default;
};

FALCOR_ENUM_CLASS_OPERATORS(IScene::UpdateFlags);
}  // namespace Falcor
