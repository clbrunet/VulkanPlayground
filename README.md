# VulkanPlayground

Voxel traversal and Vulkan Playground

<img width="1920" height="1032" alt="VulkanPlayground screenshot" src="https://github.com/user-attachments/assets/cc2fb67f-6f96-4413-8d0c-0d247bbe100e" />

## Building

First, you'll need to install [Git](https://git-scm.com/downloads), [CMake](https://cmake.org/download/) and the [Vulkan SDK 1.4.313](https://vulkan.lunarg.com/sdk/home).

Open a command prompt and run :
```sh
git clone -j4 --recurse-submodules https://github.com/clbrunet/VulkanPlayground.git && cd VulkanPlayground
```

### Linux using Make
```sh
cmake -B build && cmake --build build --parallel 4 && ./build/VulkanPlayground
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
