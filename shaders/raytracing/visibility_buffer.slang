#ifndef RENDERLAB_VISIBILITY_BUFFER_SLANG
#define RENDERLAB_VISIBILITY_BUFFER_SLANG

static const uint kInvalidVBufferInstance = 0xffffffffu;
static const uint4 kInvalidVBufferHit = uint4(kInvalidVBufferInstance, 0, 0, 0);

struct VBufferHit
{
    uint4 packed;

    property uint instanceIndex { get { return packed.x; } }
    property uint primitiveIndex { get { return packed.y; } }
    property float2 barycentrics { get { return asfloat(packed.zw); } }

    bool isValid() { return instanceIndex != kInvalidVBufferInstance; }
};

#endif
