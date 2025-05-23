# Little Vulkan Engine

A video tutorial series introducing computer graphics for [Vulkan®](https://www.khronos.org/vulkan/), the new generation graphics and compute API from Khronos. The focus of this tutorial is to be approachable to newcomers to computer graphics and graphics APIs, explaining not just the Vulkan API but also computer graphics theory, mathematics and engine architecture.

## <a name="Building"></a> Building

### <a name="UnixBuild"></a> Unix Build Instructions

- Install the dependencies: cmake, glm, vulkan and glfw

- For example
  ```
    sudo apt install vulkan-tools
    sudo apt install libvulkan-dev
    sudo apt install vulkan-validationlayers-dev spirv-tools
    sudo apt install libglfw3-dev
    sudo apt install libglm-dev
    sudo apt install cmake
  ```
- To Build
  ```
   cd LittleVulkanEngine
   ./unixBuild.sh
  ```

### <a name="MacOSBuild"></a> MacOS Build Instructions

#### Install Dependencies

- [Download and install MacOS Vulkan sdk](https://vulkan.lunarg.com/)
- [Download and install Homebrew](https://brew.sh/)

- Then in a terminal window

  ```
    brew install cmake
    brew install glfw
    brew install glm
  ```

- To Build
  ```
   cd littleVulkanEngine
   ./unixBuild.sh
  ```

### <a name="WindowsBuild"></a> Windows build instructions

- [Download and install Windows Vulkan sdk](https://vulkan.lunarg.com/)
- [Download and install Windows cmake x64 installer](https://cmake.org/download/)
  - Make sure to select "Add cmake to the system Path for all users"
- [Download GLFW](https://www.glfw.org/download.html) (64-bit precompiled binary)
- [Download GLM](https://github.com/g-truc/glm/releases)
- Download and open the project and rename "envWindowsExample.cmake" to ".env.cmake"
- Update the filepath variables in .env.cmake to your installation locations

#### Building for Visual Studio 2019

- In windows powershell

```
 cd littleVulkanEngine
 mkdir build
 cmake -S . -B .\build\
```

- If cmake finished successfully, it will create a BurnhopeEngine.sln file in the build directory that can be opened with visual studio. In visual studio right click the Shaders project -> build, to build the shaders. Right click the BurnhopeEngine project -> set as startup project. Change from debug to release, and then build and start without debugging.

#### Building for minGW

- [Download and install MinGW-w64](https://www.mingw-w64.org/downloads/), you probably want MingW-W64-builds/
- Make sure MinGW has been added to your Path
- Also set MINGW_PATH variable in the project's .env.cmake
- In windows powershell

```
 cd littleVulkanEngine
 ./mingwBuild.bat
```

- This will build the project to build/BurnhopeEngine.exe, double click in file explorer to open and run
