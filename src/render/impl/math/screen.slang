#ifndef RENDERLAB_SCREEN_SLANG
#define RENDERLAB_SCREEN_SLANG
// RHI uses clip-space Y up and texture/screen coordinates with their origin at the top left.
float2 uvToClip(float2 uv) { return uv * float2(2.0, -2.0) + float2(-1.0, 1.0); }
float2 clipToUv(float2 clip) { return clip * float2(0.5, -0.5) + 0.5; }
#endif
