# OpenGL ES / ANGLE Learning Map

这个目录用于系统学习 ANGLE、OpenGL ES 以及跨平台渲染抽象。它收录了五条主线仓库：

1. `ANGLE`: 学 OpenGL ES 到 D3D/Vulkan/Metal/Desktop GL 等后端的翻译层。
2. `GLFW`: 学窗口、输入、OpenGL/OpenGL ES 上下文创建。
3. `bgfx`: 学成熟跨后端渲染抽象。
4. `DiligentEngine`: 学现代图形 API 抽象和多后端工程组织。
5. `Magnum`: 学更现代的 C++ OpenGL/OpenGL ES 封装和示例。

## 目录结构

```text
OpenGL-ES-ANGLE-Learning/
  README.md
  repos/
    angle/
    glfw/
    bgfx/
    DiligentEngine/
    magnum/
    support/
      bx/
      bimg/
      corrade/
      magnum-examples/
  notes/
    00-environment.md
    01-angle.md
    02-glfw.md
    03-bgfx.md
    04-diligent-engine.md
    05-magnum.md
    06-visual-studio-shortcuts.md
    repo-status.md
  install/
    Debug/
      include/
      lib/
      bin/
      symbols/
```

## 推荐学习顺序

### 阶段 1: 原生 OpenGL ES 最小闭环

先看 `GLFW`。目标不是深入 GLFW，而是掌握窗口、上下文、swap buffer、输入循环。你应该能写出：

- 创建窗口。
- 请求 OpenGL ES 或 OpenGL context。
- 编译 vertex/fragment shader。
- 画一个 triangle。
- 用 RenderDoc 或调试输出确认 draw call。

阅读：`notes/02-glfw.md`

### 阶段 2: 把同一个 GLES 程序切到 ANGLE

再看 `ANGLE`。目标是理解 EGL + GLES 的应用接口，以及 ANGLE 如何把前端 GLES 调用落到平台后端。优先理解这些目录：

- `include/EGL`, `include/GLES2`, `include/GLES3`
- `src/libEGL`
- `src/libGLESv2`
- `src/libANGLE`
- `src/libANGLE/renderer`
- `samples`

阅读：`notes/01-angle.md`

### 阶段 3: 学跨后端抽象

接着看 `bgfx` 和 `DiligentEngine`。它们都不是“教你裸写 GLES”的项目，而是展示成熟库如何把 GL/GLES/D3D/Vulkan/Metal 统一成一个上层渲染 API。

阅读：

- `notes/03-bgfx.md`
- `notes/04-diligent-engine.md`

### 阶段 4: 回到更舒服的 C++ OpenGL 学习体验

最后看 `Magnum`。它比 Cinder 更适合作为 OpenGL/OpenGL ES 的现代 C++ 学习样本，代码组织清爽，文档和例子比较友好。

阅读：`notes/05-magnum.md`

## 和 Cinder 的关系

Cinder 适合当工程集成参考：窗口、平台层、渲染器抽象、Cocoa/Obj-C++ 桥接、示例组织都值得看。但如果主目标是 ANGLE 和 OpenGL ES，主线应放在 ANGLE + GLFW + bgfx/Diligent/Magnum 上。

## 快速入口

从这里开始：

1. 打开 `notes/00-environment.md` 检查工具链。
2. 打开 `notes/02-glfw.md` 跑 GLFW examples。
3. 打开 `notes/01-angle.md` 理解 ANGLE 的正确同步和构建方式。
4. 打开 `notes/repo-status.md` 查看当前本地 clone 状态。
5. 打开 `notes/06-visual-studio-shortcuts.md` 查阅 VS 常用快捷键。

## 本地 SDK

已经准备好的 Debug 依赖安装在：

```text
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install\Debug
```

这里包含 ANGLE 的 EGL/GLES 头文件、`libEGL.dll.lib`、`libGLESv2.dll.lib`、运行时 DLL，以及 GLFW 的 `glfw3.lib`、GLM 数学库和 GLFW CMake package。VS IDE 里写学习 demo 时优先使用这个目录。

VS Lab 共享属性表位于：

```text
D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\labs\props\AngleGlfwDebug.props
```

它统一配置了 ANGLE、GLFW、GLM、UTF-8 编译选项和运行时 DLL 拷贝命令。
