# RenderingEngine

RenderingEngine is the Vulkan renderer extracted from Drone Piloting. It can be
built directly, or included from another CMake project with `add_subdirectory`.

The library sources and public headers live under `renderer/`; examples and
third-party dependencies remain separate at the repository root.

## Clone and build the example

```powershell
git clone --recursive <renderer-repository-url> Renderer
cmake -S Renderer -B Renderer/build
cmake --build Renderer/build --config Debug
Renderer/build/examples/glfw_consumer/Debug/renderingEngineGlfwExample.exe
```

The example requires a Vulkan SDK and a Vulkan-capable display. When this
repository is consumed as a submodule, examples are disabled by default.

## CMake target

Link consumers to `RenderingEngine::RenderingEngine` and include public headers
with the `renderer/` prefix, for example `#include <renderer/RenderingEngine.hpp>`.
A parent project may set
`RENDERING_ENGINE_USE_PROVIDED_VULKAN=ON` after defining `Vulkan::Vulkan` and
`Vulkan::glslc`; otherwise RenderingEngine locates the Vulkan SDK itself.
