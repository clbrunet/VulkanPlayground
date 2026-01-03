# VulkanPlayground

Voxel traversal and Vulkan Playground

<img width="1920" height="1032" alt="Screenshot 2026-01-03 205059" src="https://github.com/user-attachments/assets/2c20b56e-a5b4-41d3-ba9c-9dfa6c969bf5" />

## Building

First, you'll need to install [Git](https://git-scm.com/downloads), [Git LFS](https://git-lfs.com/), [CMake](https://cmake.org/download/) and the [Vulkan SDK 1.4.313](https://vulkan.lunarg.com/sdk/home).

Open a command prompt and run :
```sh
git clone -j4 --recurse-submodules https://github.com/clbrunet/VulkanPlayground.git && cd VulkanPlayground
```

### Linux using Make
Install a C++ compiler like clang, [GLFW dependencies](https://www.glfw.org/docs/latest/compile_guide.html#compile_deps) and [Native File Dialog Extended dependencies](https://github.com/btzy/nativefiledialog-extended?tab=readme-ov-file#linux)
```sh
cmake -B build -G "Unix Makefiles" && cmake --build build --parallel 4 && ./build/VulkanPlayground
```

### Windows using Visual Studio 2022
Ensure that you have [Visual Studio 2022](https://visualstudio.microsoft.com/vs/) installed, then run the following command :
```sh
cmake -B build -G "Visual Studio 17 2022"
```
You can now open the project solution file `build/VulkanPlayground.sln` and press the start button.

### Windows using MinGW-w64
Follow the MinGW-w64 installation instructions [here](https://code.visualstudio.com/docs/cpp/config-mingw#_installing-the-mingww64-toolchain), then run :
```sh
cmake -B build -G "MinGW Makefiles" && cmake --build build --parallel 4 && ./build/VulkanPlayground.exe
```

## Dependencies
* [Vulkan SDK 1.4.313](https://vulkan.lunarg.com/sdk/home)
* [VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
* [assimp](https://github.com/assimp/assimp)
* [stb_image](https://github.com/nothings/stb)
* [GLFW](https://github.com/glfw/glfw)
* [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended)
* [glm](https://github.com/g-truc/glm)
* [Dear ImGui](https://github.com/ocornut/imgui)
* [Hosek-Wilkie Sky Model](https://cgg.mff.cuni.cz/projects/SkylightModelling/)
