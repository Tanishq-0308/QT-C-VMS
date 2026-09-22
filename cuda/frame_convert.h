#pragma once

#include <cuda_runtime.h>
#include <stdint.h>

// Source pixel layouts the recorder can convert on the GPU
enum class FrameConvertFormat : int
{
    UYVY = 0, // DeckLink bmdFormat8BitYUV
    ARGB = 1, // DeckLink bmdFormat8BitARGB
    BGRA = 2, // DeckLink bmdFormat8BitBGRA
};

// Convert one frame (device memory) to NV12 planes (device memory), optionally mirrored.
// Width and height must be even. Returns a cudaError_t value (0 = success).
extern "C" int launchToNv12(FrameConvertFormat format,
                            const uint8_t* src, int srcPitch,
                            uint8_t* dstY, int dstYPitch,
                            uint8_t* dstUV, int dstUVPitch,
                            int width, int height, bool flipH, bool flipV,
                            cudaStream_t stream);
