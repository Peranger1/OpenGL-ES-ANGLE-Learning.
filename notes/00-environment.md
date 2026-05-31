# 00. Environment

## Windows 推荐环境

建议准备：

- Visual Studio 2022，安装 `Desktop development with C++`。
- CMake 3.20+。
- Git。
- Python 3。
- Ninja，可选但推荐。
- Vulkan SDK，可选，用于 DiligentEngine/bgfx Vulkan 后端实验。
- RenderDoc，可选，用于抓帧分析。
- Android Studio + Android NDK，可选，用于真正的 OpenGL ES / Android 路线。

当前机器已确认有 CMake：

```powershell
cmake --version
```

## 通用构建目录约定

所有构建产物建议放在仓库内的 `build/` 或 `out/` 中，不提交，不混到源码目录：

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Visual Studio 生成器示例：

```powershell
cmake -S . -B build\vs2022 -G "Visual Studio 17 2022" -A x64
cmake --build build\vs2022 --config Debug
```

Ninja 示例：

```powershell
cmake -S . -B build\ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build\ninja
```

## 学习时要分清三层

- API 层：OpenGL ES / EGL / OpenGL。
- 平台层：Win32、Cocoa、X11/Wayland、Android NativeActivity、GLFW、SDL。
- 后端层：D3D11/D3D12、Vulkan、Metal、Desktop GL、GLES driver。

ANGLE 重点在 API 层到后端层的翻译；GLFW 重点在平台层；bgfx/DiligentEngine/Magnum 重点在上层渲染抽象。

