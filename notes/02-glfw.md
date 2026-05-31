# 02. GLFW

本地路径：

```text
repos/glfw
```

GLFW 负责窗口、输入、monitor、OpenGL/OpenGL ES/Vulkan context 创建。它很适合当 OpenGL ES 学习的第一个落地点。

## 编译 GLFW

当前已经安装到统一 Debug SDK：

```text
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug
```

实际使用的命令：

```powershell
$root="D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning"
$prefix="$root\install\Debug"
$build="$root\repos\glfw\build\vs2022-debug"
cmake -S "$root\repos\glfw" -B "$build" -G "Visual Studio 17 2022" -A x64 `
  -D CMAKE_INSTALL_PREFIX="$prefix" `
  -D GLFW_BUILD_EXAMPLES=OFF `
  -D GLFW_BUILD_TESTS=OFF `
  -D GLFW_BUILD_DOCS=OFF
cmake --build "$build" --config Debug --target INSTALL
```

安装结果：

```text
install\Debug\include\GLFW\glfw3.h
install\Debug\include\GLFW\glfw3native.h
install\Debug\lib\glfw3.lib
install\Debug\lib\cmake\glfw3\glfw3Config.cmake
```

Visual Studio 2022：

```powershell
cd D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\glfw
cmake -S . -B build\vs2022 -G "Visual Studio 17 2022" -A x64 -D GLFW_BUILD_EXAMPLES=ON -D GLFW_BUILD_TESTS=ON
cmake --build build\vs2022 --config Debug
```

Ninja：

```powershell
cd D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\glfw
cmake -S . -B build\ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug -D GLFW_BUILD_EXAMPLES=ON -D GLFW_BUILD_TESTS=ON
cmake --build build\ninja
```

## 先跑什么

构建后重点看：

```text
examples/
tests/
docs/
```

推荐先跑：

- `triangle-opengl`
- `gears`
- `sharing`
- `events`

实际生成的 exe 名称取决于 CMake 版本和 GLFW 当前 CMakeLists。

## 创建 OpenGL ES context 的关键 API

GLFW 中创建 GLES context 的关键 hint：

```cpp
glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
```

如果使用 ANGLE，通常还需要链接/加载 ANGLE 提供的 EGL/GLES 库。GLFW 本身不等于 ANGLE，它只是负责窗口和 context 入口。

配合 ANGLE 时建议额外设置：

```cpp
glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
```

同时确保运行时能找到 `install\Debug\bin\libEGL.dll` 和 `install\Debug\bin\libGLESv2.dll`。

## 学习任务

1. 改一个 GLFW example，让它请求 OpenGL ES 3.0。
2. 写一个最小 triangle。
3. 加入 shader compile error log。
4. 加入 VBO/VAO/EBO。
5. 尝试把窗口层保留，GLES 实现切换到 ANGLE。
