if not exist build mkdir build
cd build
cmake .. -A x64 -DCMAKE_TOOLCHAIN_FILE="C:\Users\Professional\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build . --config Release