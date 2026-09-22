// Test helper: occupies the GPU for `ms` milliseconds with a single spinning thread on the legacy default
// stream of the CUDA runtime (primary context, shared with the recorder). The recorder's conversion kernel
// and its cuCtxSynchronize() queue behind it, i.e. the encoder stalls while capture keeps pushing.
#include <cuda.h>
#include <cuda_runtime.h>

__global__ void spinKernel(long long cycles)
{
    const long long t0 = clock64();
    while (clock64() - t0 < cycles) {}
}

extern "C" int gpuStallAsync(int ms)
{
    int khz = 0;
    cudaError_t e = cudaDeviceGetAttribute(&khz, cudaDevAttrClockRate, 0);
    if (e != cudaSuccess) return (int)e;
    spinKernel<<<1, 1>>>((long long)khz * ms);
    return (int)cudaGetLastError();
}

// Same, but inside a given driver context (the recorder's own context)
extern "C" int gpuStallInContext(CUcontext ctx, int ms)
{
    if (!ctx || cuCtxPushCurrent(ctx) != CUDA_SUCCESS)
        return -1;
    const int r = gpuStallAsync(ms);
    CUcontext dummy;
    cuCtxPopCurrent(&dummy);
    return r;
}
