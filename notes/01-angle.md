# 01. ANGLE

本地路径：

```text
repos/angle
repos/angle-devsetup
```

官方定位：ANGLE 是 OpenGL ES 的实现层，把 OpenGL ES API 调用翻译到平台可用的后端，例如 D3D11、Vulkan、Desktop OpenGL、OpenGL ES、Metal 等。

## 当前本地状态

`repos/angle` 是最早 clone 的源码阅读副本，之前普通 submodule 同步产生的 `.git/modules` 缓存已经清理。

`repos/angle-devsetup` 是按 `doc/DevSetup.md` 创建的 Windows 可编译工作区：

- 使用 `depot_tools`
- 设置 `DEPOT_TOOLS_WIN_TOOLCHAIN=0`
- 使用 `fetch angle`
- 使用 `gclient sync --no-history -j1` 完成依赖同步
- 使用 `gn gen` 生成 VS2022 solution

查看状态：

```powershell
git -C D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\angle status --short
git -C D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\angle rev-parse --short HEAD
git -C D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\angle-devsetup rev-parse --short HEAD
```

## Windows DevSetup 实际命令

代理：

```powershell
$Env:http_proxy="http://127.0.0.1:7890"
$Env:https_proxy="http://127.0.0.1:7890"
$Env:HTTP_PROXY=$Env:http_proxy
$Env:HTTPS_PROXY=$Env:https_proxy
```

depot_tools：

```powershell
$Env:PATH="D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\support\depot_tools;$Env:PATH"
$Env:DEPOT_TOOLS_WIN_TOOLCHAIN="0"
```

VS2022 自定义路径：

```powershell
$Env:vs2022_install="D:\CodePrograms\Microsoft Visual Studio\2022\Community"
$Env:GYP_MSVS_OVERRIDE_PATH=$Env:vs2022_install
$Env:WINDOWSSDKDIR="C:\Program Files (x86)\Windows Kits\10"
```

按文档获取源码：

```powershell
cd D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\angle-devsetup
fetch angle
```

如果遇到 `chromium.googlesource.com` 短期限流 `HTTP 429 RESOURCE_EXHAUSTED`，降低并发继续：

```powershell
gclient sync --no-history -j1
```

生成 VS2022 solution：

```powershell
cd D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\angle-devsetup
$nin="D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\angle-devsetup\third_party\ninja\ninja.exe"
gn gen out/Debug --sln=angle-debug --ide=vs2022 --ninja-executable="$nin" --args="use_siso=false"
```

生成结果：

```text
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\angle-devsetup\out\Debug\angle-debug.sln
```

注意：ANGLE 官方文档提示 GN 生成的 VS solution 更适合浏览、调试、单 target/单文件构建；完整构建仍推荐命令行 `autoninja -C out/Debug`。

## 已完成的 Debug 构建

本机已经完成：

```powershell
cd D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\angle-devsetup
autoninja -C out/Debug libEGL libGLESv2
```

这次构建前对 `out/Debug` 做过一次 `gn clean`，原因是该目录之前带有 Siso/Ninja 切换状态；随后用 `use_siso=false` 重新 `gn gen`，再用 Ninja 成功构建。

构建输出已整理到统一 SDK：

```text
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug\include\EGL
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug\include\GLES2
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug\include\GLES3
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug\include\KHR
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug\lib\libEGL.dll.lib
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug\lib\libGLESv2.dll.lib
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug\bin\libEGL.dll
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug\bin\libGLESv2.dll
```

运行自己写的 demo 时，把 `install\Debug\bin` 放到 `PATH`，或者把需要的 DLL 复制到 exe 所在目录。

## 推荐阅读目录

```text
include/EGL/
include/GLES2/
include/GLES3/
src/libEGL/
src/libGLESv2/
src/libANGLE/
src/libANGLE/renderer/
samples/
doc/
```

重点理解：

- `EGLDisplay`, `EGLContext`, `EGLSurface` 如何映射到平台后端。
- GLES 函数入口如何进入 `libGLESv2`。
- `libANGLE` 如何维护 state、caps、extensions。
- `renderer` 下不同后端如何实现同一套抽象。

## 完整构建建议

ANGLE 官方文档的核心命令是 GN + Ninja：

```powershell
gn gen out\Debug
ninja -C out\Debug
```

但在干净机器上，前置条件通常是：

1. 安装 depot_tools。
2. 用 Chromium 风格流程获取依赖。
3. 让 `gclient sync` 下载 DEPS 中声明的第三方库。

建议把完整 ANGLE 构建作为独立阶段处理，不要和普通 CMake 仓库混在一起。

## 最小学习路线

1. 先从 `samples` 看一个最小 GLES demo，例如 clear、triangle、texture。
2. 跟踪 EGL 初始化：display -> config -> surface -> context -> make current。
3. 跟踪 GLES 调用：`glDrawArrays` / `glDrawElements` 进入 `src/libGLESv2`。
4. 选择一个后端看到底，例如 D3D11 或 Vulkan。
5. 对比同一个 API 在不同 renderer 后端如何分发。

## 和 GLFW 结合的小目标

先用 GLFW 写一个裸 GLES triangle，然后将链接库替换为 ANGLE 的 `libEGL` / `libGLESv2`。这比一上来读完整 ANGLE 更容易建立手感。
