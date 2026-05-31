# 05. Magnum

本地路径：

```text
repos/magnum
repos/support/corrade
repos/support/magnum-examples
```

Magnum 是现代 C++ 图形中间件，OpenGL/OpenGL ES 支持和文档都比较友好。它适合补上“写现代 C++ OpenGL 代码的手感”。

## 依赖关系

Magnum 的核心依赖是 Corrade。当前已经把 Corrade 和 examples 放在：

```text
repos/support/corrade
repos/support/magnum-examples
```

## 编译 Corrade

```powershell
cd D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\support\corrade
cmake -S . -B build\vs2022 -G "Visual Studio 17 2022" -A x64 -D CMAKE_INSTALL_PREFIX=D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install
cmake --build build\vs2022 --config Debug
cmake --install build\vs2022 --config Debug
```

## 编译 Magnum

```powershell
cd D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\magnum
cmake -S . -B build\vs2022 -G "Visual Studio 17 2022" -A x64 -D CMAKE_PREFIX_PATH=D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install -D CMAKE_INSTALL_PREFIX=D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install
cmake --build build\vs2022 --config Debug
cmake --install build\vs2022 --config Debug
```

## 编译 Magnum Examples

```powershell
cd D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\repos\support\magnum-examples
cmake -S . -B build\vs2022 -G "Visual Studio 17 2022" -A x64 -D CMAKE_PREFIX_PATH=D:\Desktop\AI-Agent\OpenGL-ES-ANGLE-Learning\install
cmake --build build\vs2022 --config Debug
```

## 先看什么

```text
repos/magnum/src/Magnum/GL/
repos/magnum/src/Magnum/Platform/
repos/support/magnum-examples/src/
```

Magnum 对你的价值：

- 把 OpenGL 对象封装成 RAII C++ 类型。
- 示例比大型引擎更轻。
- 适合写自己的实验项目。

## 学习任务

1. 跑一个最小 example。
2. 看 `Magnum::GL::Buffer`, `Texture`, `Shader`, `Mesh`。
3. 对比原生 GLES 写法和 Magnum 封装写法。
4. 尝试移植一个 GLFW triangle 到 Magnum。

