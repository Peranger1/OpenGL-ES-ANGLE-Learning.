# Repo Status

创建时间：2026-05-30

## 主线仓库

```text
repos/angle           main    5a008cb670
repos/angle-devsetup  main    357791c549
repos/glfw            master  b00e6a8
repos/bgfx            master  0d4141d
repos/DiligentEngine  master  f6b129e
repos/magnum          master  774ffdb
```

## 配套仓库

```text
repos/support/bx               master  7dc65d7
repos/support/bimg             master  43fef5f
repos/support/corrade          master  120098e
repos/support/magnum-examples  master  4e7b16e
```

## 克隆策略

- `glfw`, `bgfx`, `DiligentEngine`, `magnum` 使用浅克隆或浅递归克隆，适合学习和本地编译示例。
- `angle` 是源码阅读副本，普通 submodule 缓存已清理。
- `angle-devsetup` 是按 `repos/angle-devsetup/doc/DevSetup.md` 创建的 Windows 可编译副本，已完成 `gclient sync --no-history -j1` 并生成 VS2022 solution。

## 更新命令

主线仓库：

```powershell
git -C D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\glfw pull --ff-only
git -C D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\bgfx pull --ff-only
git -C D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\DiligentEngine pull --ff-only
git -C D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\magnum pull --ff-only
```

配套仓库：

```powershell
git -C D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\support\bx pull --ff-only
git -C D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\support\bimg pull --ff-only
git -C D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\support\corrade pull --ff-only
git -C D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\support\magnum-examples pull --ff-only
```

ANGLE 建议先读源码和 samples。完整同步请参考 `repos/angle/doc/BuildingAngleForChromiumDevelopment.md`。

## ANGLE VS Solution

```text
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\angle-devsetup\out\Debug\angle-debug.sln
```

生成命令：

```powershell
$Env:http_proxy="http://127.0.0.1:7890"
$Env:https_proxy="http://127.0.0.1:7890"
$Env:HTTP_PROXY=$Env:http_proxy
$Env:HTTPS_PROXY=$Env:https_proxy
$Env:DEPOT_TOOLS_WIN_TOOLCHAIN="0"
$Env:vs2022_install="D:\CodePrograms\Microsoft Visual Studio\2022\Community"
$Env:GYP_MSVS_OVERRIDE_PATH=$Env:vs2022_install
$Env:WINDOWSSDKDIR="C:\Program Files (x86)\Windows Kits\10"
$Env:PATH="D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\support\depot_tools;$Env:PATH"
$nin="D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\angle-devsetup\third_party\ninja\ninja.exe"
gn gen out/Debug --sln=angle-debug --ide=vs2022 --ninja-executable="$nin" --args="use_siso=false"
```

## 已安装 Debug SDK

统一安装前缀：

```text
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug
```

已验证存在：

```text
include\EGL\egl.h
include\GLES2\gl2.h
include\GLES3\gl3.h
include\KHR\khrplatform.h
include\GLFW\glfw3.h
include\glm\glm.hpp
lib\libEGL.dll.lib
lib\libGLESv2.dll.lib
lib\glfw3.lib
lib\glm.lib
bin\libEGL.dll
bin\libGLESv2.dll
bin\libc++.dll
lib\cmake\glfw3\glfw3Config.cmake
```

VS IDE 手动配置：

```text
C/C++ -> Additional Include Directories:
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug\include

Linker -> Additional Library Directories:
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug\lib

Linker -> Input -> Additional Dependencies:
glfw3.lib
glm.lib
libEGL.dll.lib
libGLESv2.dll.lib
user32.lib
gdi32.lib
shell32.lib
```

运行前：

```powershell
$Env:PATH="D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug\bin;$Env:PATH"
```
