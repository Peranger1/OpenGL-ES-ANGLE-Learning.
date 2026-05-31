# 03. bgfx

本地路径：

```text
repos/bgfx
repos/support/bx
repos/support/bimg
```

bgfx 是跨平台渲染库，支持 D3D、Metal、Vulkan、OpenGL、OpenGL ES 等。它不适合当第一份 GLES 教材，但很适合学习“成熟跨后端 renderer 怎么设计”。

## 目录关系

bgfx 通常要求 `bx`、`bimg`、`bgfx` 是同级目录。当前目录为了保持主线清晰，把 `bx` 和 `bimg` 放在 `repos/support` 下。若按官方脚本构建，需要临时创建同级结构，或把三个仓库放到同一个父目录。

建议方式：

```text
some-parent/
  bx/
  bimg/
  bgfx/
```

当前仓库可复制或移动成上述结构，也可以新建 junction。

## Windows 构建思路

官方常见路径是使用 bx 的构建脚本生成项目，然后构建 examples。具体命令会随 bgfx 版本变化，先从这些文件看起：

```text
repos/bgfx/docs/
repos/bgfx/scripts/
repos/bgfx/examples/
repos/bgfx/tools/
```

如果使用 Visual Studio，优先目标是构建 examples，而不是一开始集成到自己的项目。

## 先看什么代码

```text
examples/00-helloworld
examples/01-cubes
examples/common
src/renderer_gl.cpp
src/renderer_d3d11.cpp
src/renderer_vk.cpp
src/renderer_mtl.mm
```

对你的目标最有价值的是：

- `src/renderer_gl.cpp`: OpenGL/OpenGL ES 后端实现。
- `examples/common`: 跨平台窗口与输入 glue。
- shader 编译工具链：bgfx 不直接吃普通 GLSL，而是有自己的 shaderc 流程。

## 学习任务

1. 跑 `00-helloworld` 和 `01-cubes`。
2. 强制选择 OpenGL 或 OpenGL ES 后端。
3. 跟踪 `bgfx::init` 到 renderer backend 的选择。
4. 跟踪一个 draw call 如何进入 encoder、command buffer、renderer。
5. 对比 bgfx 和 DiligentEngine 的 device/context 抽象。

