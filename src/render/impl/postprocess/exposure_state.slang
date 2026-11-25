#ifndef RENDERLAB_EXPOSURE_STATE_SLANG
#define RENDERLAB_EXPOSURE_STATE_SLANG

struct ExposureState
{
    float ev100;
    float exposureScale;
    uint automaticValid;
    uint padding;
};

#endif
