#ifndef RENDERLAB_EXPOSURE_COMMON_SLANG
#define RENDERLAB_EXPOSURE_COMMON_SLANG

static const uint kExposureHistogramBinCount = 256;
static const float kExposureLogLuminanceMin = -20.0;
static const float kExposureLogLuminanceRange = 40.0;
static const float kExposureCalibration = 1.2;

struct ExposureState
{
    float ev100;
    float exposureScale;
    uint automaticValid;
    uint padding;
};

#endif
