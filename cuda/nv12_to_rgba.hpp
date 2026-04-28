#pragma once
#include <cuda_runtime.h>
#include <cuda_surface_types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Declare the CUDA kernel launcher
void launch_nv12_to_rgba(
    uint8_t* yPlane,
    uint8_t* uvPlane,
    int yPitch,
    int uvPitch,
    int width,
    int height,
    cudaSurfaceObject_t surfaceOut,
    int rotationMode // 0 = 0°, 1 = 90°, 2 = 180°, 3 = 270°
);

#ifdef __cplusplus
}
#endif
