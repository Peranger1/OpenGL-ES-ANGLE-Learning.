# 04. DiligentEngine

本地路径：

```text
repos/DiligentEngine
```

DiligentEngine 是现代跨平台图形库，支持 D3D11、D3D12、OpenGL、OpenGLES、Vulkan、Metal、WebGPU 等。它比 bgfx 更像“现代显式图形 API 抽象”的教材。

## 编译 Windows 示例

Visual Studio 2022：

```powershell
cd D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\DiligentEngine
cmake -S . -B build\Win64 -G "Visual Studio 17 2022" -A x64
cmake --build build\Win64 --config Debug
```

构建后可从 solution 中启动 sample，也可以到 build 输出目录里运行。Windows 下常见运行模式：

```powershell
SampleName.exe --mode gl
SampleName.exe --mode d3d11
SampleName.exe --mode d3d12
SampleName.exe --mode vk
```

如果只关心 OpenGL/OpenGLES，优先用 `--mode gl`。

## 先看什么

```text
DiligentCore/Graphics/GraphicsEngineOpenGL/
DiligentCore/Graphics/GraphicsEngineD3D11/
DiligentCore/Graphics/GraphicsEngineVulkan/
DiligentSamples/
```

重点概念：

- Render device
- Device context
- Swap chain
- Pipeline state
- Shader resource binding
- Buffer / Texture / View

## 适合你的原因

ANGLE 让你看到 GLES 如何落到后端；DiligentEngine 让你看到“如果自己设计跨后端抽象，接口应该长什么样”。它对理解 ANGLE 的 renderer 后端也有帮助。

## 学习任务

1. 构建并运行一个 sample。
2. 用 `--mode gl` 跑 OpenGL 后端。
3. 切到 `--mode d3d11` 或 `--mode vk`，比较应用层代码是否变化。
4. 阅读 `GraphicsEngineOpenGL` 的 texture、buffer、pipeline 实现。
5. 总结 DiligentEngine 和 bgfx 在抽象粒度上的差异。

