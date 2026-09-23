// GPU colour conversion for the recorder: DeckLink capture formats -> NV12 (input for NVENC).
//
// Each thread converts one 2x2 pixel block, so width and height must be even.
// flipH / flipV mirror the picture the same way the preview's flip steps do.

#include "frame_convert.h"

#include <cuda_runtime.h>
#include <stdint.h>

namespace {

__device__ __forceinline__ uint8_t clampU8(float v)
{
    return static_cast<uint8_t>(fminf(fmaxf(v + 0.5f, 0.0f), 255.0f));
}

// 8-bit UYVY (DeckLink bmdFormat8BitYUV): bytes U0 Y0 V0 Y1 for each horizontal pixel pair.
// It is already Y'CbCr, so only the chroma is subsampled vertically (4:2:2 -> 4:2:0).
__global__ void uyvyToNv12Kernel(const uint8_t* src, int srcPitch,
                                 uint8_t* dstY, int dstYPitch,
                                 uint8_t* dstUV, int dstUVPitch,
                                 int width, int height, bool flipH, bool flipV)
{
    const int bx = blockIdx.x * blockDim.x + threadIdx.x; // output pixel pair (2 px wide)
    const int by = blockIdx.y * blockDim.y + threadIdx.y; // output row pair
    if (bx >= width / 2 || by >= height / 2)
        return;

    const int pairs = width / 2;
    const int srcPair = flipH ? (pairs - 1 - bx) : bx;

    int uSum = 0, vSum = 0;
    for (int r = 0; r < 2; ++r)
    {
        const int outRow = by * 2 + r;
        const int srcRow = flipV ? (height - 1 - outRow) : outRow;
        const uint8_t* p = src + static_cast<size_t>(srcRow) * srcPitch + srcPair * 4;

        uint8_t y0 = p[1], y1 = p[3];
        if (flipH) { uint8_t t = y0; y0 = y1; y1 = t; }

        uint8_t* outY = dstY + static_cast<size_t>(outRow) * dstYPitch + bx * 2;
        outY[0] = y0;
        outY[1] = y1;

        uSum += p[0];
        vSum += p[2];
    }

    uint8_t* outUV = dstUV + static_cast<size_t>(by) * dstUVPitch + bx * 2;
    outUV[0] = static_cast<uint8_t>((uSum + 1) / 2);
    outUV[1] = static_cast<uint8_t>((vSum + 1) / 2);
}

// 8-bit RGB with alpha (DeckLink bmdFormat8BitARGB / bmdFormat8BitBGRA) -> BT.709 limited range.
__global__ void rgbaToNv12Kernel(const uint8_t* src, int srcPitch,
                                 uint8_t* dstY, int dstYPitch,
                                 uint8_t* dstUV, int dstUVPitch,
                                 int width, int height, bool flipH, bool flipV,
                                 int rOff, int gOff, int bOff)
{
    const int bx = blockIdx.x * blockDim.x + threadIdx.x;
    const int by = blockIdx.y * blockDim.y + threadIdx.y;
    if (bx >= width / 2 || by >= height / 2)
        return;

    float rSum = 0.f, gSum = 0.f, bSum = 0.f;
    for (int r = 0; r < 2; ++r)
    {
        const int outRow = by * 2 + r;
        const int srcRow = flipV ? (height - 1 - outRow) : outRow;
        uint8_t* outY = dstY + static_cast<size_t>(outRow) * dstYPitch + bx * 2;

        for (int c = 0; c < 2; ++c)
        {
            const int outCol = bx * 2 + c;
            const int srcCol = flipH ? (width - 1 - outCol) : outCol;
            const uint8_t* p = src + static_cast<size_t>(srcRow) * srcPitch + srcCol * 4;
            const float R = p[rOff], G = p[gOff], B = p[bOff];

            outY[c] = clampU8(16.f + 0.1826f * R + 0.6142f * G + 0.0620f * B);
            rSum += R; gSum += G; bSum += B;
        }
    }

    const float R = rSum * 0.25f, G = gSum * 0.25f, B = bSum * 0.25f;
    uint8_t* outUV = dstUV + static_cast<size_t>(by) * dstUVPitch + bx * 2;
    outUV[0] = clampU8(128.f - 0.1006f * R - 0.3386f * G + 0.4392f * B);
    outUV[1] = clampU8(128.f + 0.4392f * R - 0.3989f * G - 0.0403f * B);
}

} // namespace

extern "C" int launchToNv12(FrameConvertFormat format,
                            const uint8_t* src, int srcPitch,
                            uint8_t* dstY, int dstYPitch,
                            uint8_t* dstUV, int dstUVPitch,
                            int width, int height, bool flipH, bool flipV,
                            cudaStream_t stream)
{
    if ((width & 1) || (height & 1) || width <= 0 || height <= 0)
        return static_cast<int>(cudaErrorInvalidValue);

    const dim3 block(16, 16);
    const dim3 grid((width / 2 + block.x - 1) / block.x, (height / 2 + block.y - 1) / block.y);

    switch (format)
    {
    case FrameConvertFormat::UYVY:
        uyvyToNv12Kernel<<<grid, block, 0, stream>>>(src, srcPitch, dstY, dstYPitch, dstUV, dstUVPitch,
                                                     width, height, flipH, flipV);
        break;
    case FrameConvertFormat::ARGB: // bytes A R G B
        rgbaToNv12Kernel<<<grid, block, 0, stream>>>(src, srcPitch, dstY, dstYPitch, dstUV, dstUVPitch,
                                                     width, height, flipH, flipV, 1, 2, 3);
        break;
    case FrameConvertFormat::BGRA: // bytes B G R A
        rgbaToNv12Kernel<<<grid, block, 0, stream>>>(src, srcPitch, dstY, dstYPitch, dstUV, dstUVPitch,
                                                     width, height, flipH, flipV, 2, 1, 0);
        break;
    default:
        return static_cast<int>(cudaErrorInvalidValue);
    }

    return static_cast<int>(cudaGetLastError());
}
