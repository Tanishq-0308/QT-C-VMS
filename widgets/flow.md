✅ FUNCTION COVERAGE CHECKLIST

| Function Name                          | Purpose                                                        | ✅ Covered |
| -------------------------------------- | -------------------------------------------------------------- | --------- |
| `initializeGL()`                       | Initializes OpenGL context, texture, shaders, and CUDA interop | ✅ Yes     |
| `resizeGL(int w, int h)`               | Resizes OpenGL viewport                                        | ✅ Yes     |
| `paintGL()`                            | Renders the current texture + draws FPS/camera text overlay    | ✅ Yes     |
| `initShaders()`                        | Compiles and links vertex + fragment shaders                   | ✅ Yes     |
| `initQuad()`                           | Sets up fullscreen quad (VAO/VBO) for rendering                | ✅ Yes     |
| `registerWithCuda()`                   | Registers the OpenGL texture with CUDA for writing             | ✅ Yes     |
| `uploadFrameToCuda(...)`               | Copies NV12 frame into CUDA-mapped OpenGL texture              | ✅ Yes     |
| `saveSnapshot(filePath)`               | Saves current framebuffer as PNG using `glReadPixels`          | ✅ Yes     |
| `setCameraInfo(id, w, h)`              | Sets camera label and resolution overlay                       | ✅ Yes     |
| `getCudaResource()` / `getTextureID()` | Utility getters                                                | ✅ Yes     |


Let me know if you'd like:

    Styled buttons with icons
    DICOM button added
    Multiple video cards (multi-camera view)


Connect recordButton to NVENC-based writer
Add timer or status indicator
Style with .qss theme