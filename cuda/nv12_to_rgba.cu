#include <cuda_runtime.h>
#include <cuda_surface_types.h>
#include <stdint.h>
#include <cstdio>

// NV12 → RGBA CUDA Kernel with rotation support
// rotationMode: 0 = 0°, 1 = 90°, 2 = 180°, 3 = 270°
__global__ void nv12_to_rgba_kernel_surface(
    const uint8_t* __restrict__ yPlane,
    const uint8_t* __restrict__ uvPlane,
    int yPitch, int uvPitch,
    int width, int height,
    cudaSurfaceObject_t surfaceOut,
    int rotationMode)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    int uvRow = y / 2;
    int uvCol = (x & ~1);

    int yIndex = y * yPitch + x;
    int uvIndex = uvRow * uvPitch + uvCol;

    uint8_t Y = yPlane[yIndex];
    uint8_t U = uvPlane[uvIndex];
    uint8_t V = uvPlane[uvIndex + 1];

    float fY = fmaxf(0.0f, float(Y) - 16.0f);
    float fU = float(U) - 128.0f;
    float fV = float(V) - 128.0f;

    float R = 1.164f * fY + 1.793f * fV;
    float G = 1.164f * fY - 0.213f * fU - 0.533f * fV;
    float B = 1.164f * fY + 2.112f * fU;

    uchar4 pixel;
    pixel.x = static_cast<uint8_t>(fminf(fmaxf(R, 0.0f), 255.0f));
    pixel.y = static_cast<uint8_t>(fminf(fmaxf(G, 0.0f), 255.0f));
    pixel.z = static_cast<uint8_t>(fminf(fmaxf(B, 0.0f), 255.0f));
    pixel.w = 255;

    // 🌀 Apply rotation
    int writeX = x;
    int writeY = y;

    switch (rotationMode) {
        case 1: writeX = width - 1 - x; writeY = height - 1 - y; break;    
                 //c
        case 3: writeX = x; writeY = y; break;    // c
        case 2: writeX =  width - 1- x; writeY =  y;  break;             //  update
        default: writeX = x; writeY = height - 1 - y; break;           // 0°
    }

    // ✅ Bounds check after rotation
    if (writeX < 0 || writeX >= width || writeY < 0 || writeY >= height) return;

    surf2Dwrite(pixel, surfaceOut, writeX * sizeof(uchar4), writeY);
}

extern "C" void launch_nv12_to_rgba(
    uint8_t* yPlane, uint8_t* uvPlane,
    int yPitch, int uvPitch,
    int width, int height,
    cudaSurfaceObject_t surfaceOut,
    int rotationMode)
{
    int launchWidth = width;
    int launchHeight = height;

    // 🌀 We still need to launch with original input dimensions
    dim3 block(16, 16);
    dim3 grid((launchWidth + block.x - 1) / block.x,
              (launchHeight + block.y - 1) / block.y);

    nv12_to_rgba_kernel_surface<<<grid, block>>>(
        yPlane, uvPlane,
        yPitch, uvPitch,
        width, height,     // input image size
        surfaceOut,
        rotationMode
    );

    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        std::printf("❌ Kernel launch failed: %s\n", cudaGetErrorString(err));
    }
}

