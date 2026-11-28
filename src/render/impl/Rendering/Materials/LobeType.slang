#ifndef RENDERLAB_LOBE_TYPE_SLANG
#define RENDERLAB_LOBE_TYPE_SLANG

enum class LobeType : uint
{
    DiffuseReflection    = 0x01,
    DiffuseTransmission  = 0x02,
    SpecularReflection   = 0x04,
    SpecularTransmission = 0x08,
    DeltaReflection      = 0x10,
    DeltaTransmission    = 0x20,

    Diffuse      = DiffuseReflection | DiffuseTransmission,
    Specular     = SpecularReflection | SpecularTransmission,
    Delta        = DeltaReflection | DeltaTransmission,
    NonDelta     = Diffuse | Specular,
    Reflection   = DiffuseReflection | SpecularReflection | DeltaReflection,
    Transmission = DiffuseTransmission | SpecularTransmission | DeltaTransmission,
    All          = Reflection | Transmission,
};

#endif
