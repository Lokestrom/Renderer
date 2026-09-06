# RenderingEngine

RenderingEngine is the Vulkan renderer extracted from Drone Piloting. It can be
built directly, or included from another CMake project with `add_subdirectory`.

The library sources and public headers live under `renderer/`, examples and
third-party dependencies remain separate at the repository root.

## Clone and build the example

```bash
git clone --recursive https://github.com/Lokestrom/Renderer.git
cd Renderer
cmake -S . -B build
cmake --build build --config <config> # Debug or Release
build/examples/glfw_consumer/Debug/renderingEngineGlfwExample.exe
```

The example requires a Vulkan SDK. When this
repository is consumed as a submodule, examples are disabled by default.

## CMake target

Link consumers to `RenderingEngine::RenderingEngine` and include public headers
with the `renderer/` prefix, for example `#include <renderer/RenderingEngine.hpp>`.
A parent project may set
`RENDERING_ENGINE_USE_PROVIDED_VULKAN=ON` after defining `Vulkan::Vulkan` and
`Vulkan::glslc`; otherwise RenderingEngine locates the Vulkan SDK itself.