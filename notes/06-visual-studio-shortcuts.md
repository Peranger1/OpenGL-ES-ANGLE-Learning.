# 06. Visual Studio 常用快捷键

这份清单面向 Visual Studio 2022 中的 C++、OpenGL ES 和 ANGLE 学习开发。

快捷键可能受到 Visual Studio 键盘映射方案、扩展和个人配置影响。如果某个按键没有生效，可以在以下位置查询或修改实际绑定：

```text
Tools -> Options -> Environment -> Keyboard
工具 -> 选项 -> 环境 -> 键盘
```

## 编辑代码

| 快捷键 | 作用 |
| --- | --- |
| `Ctrl + K, Ctrl + C` | 注释选中代码 |
| `Ctrl + K, Ctrl + U` | 取消注释 |
| `Ctrl + K, Ctrl + D` | 格式化整个文档 |
| `Ctrl + K, Ctrl + F` | 格式化选中代码 |
| `Ctrl + X` | 未选中文本时剪切整行 |
| `Ctrl + C` | 未选中文本时复制整行 |
| `Ctrl + L` | 删除整行 |
| `Alt + Up / Down` | 上下移动当前行或选中代码 |
| `Ctrl + Z` | 撤销 |
| `Ctrl + Y` | 重做 |
| `Ctrl + Space` | 触发 IntelliSense 补全 |
| `Ctrl + J` | 打开成员补全列表 |

## 查找与替换

| 快捷键 | 作用 |
| --- | --- |
| `Ctrl + F` | 在当前文件中查找 |
| `Ctrl + H` | 在当前文件中替换 |
| `Ctrl + Shift + F` | 在整个解决方案中查找 |
| `Ctrl + Shift + H` | 在整个解决方案中替换 |
| `F3` | 跳到下一个匹配项 |
| `Shift + F3` | 跳到上一个匹配项 |

## 阅读与跳转

| 快捷键 | 作用 |
| --- | --- |
| `F12` | 跳转到定义 |
| `Ctrl + F12` | 跳转到声明 |
| `Alt + F12` | 内嵌查看定义，不离开当前文件 |
| `Shift + F12` | 查找所有引用 |
| `Ctrl + -` | 返回上一个浏览位置 |
| `Ctrl + Shift + -` | 前进到下一个浏览位置 |
| `Ctrl + T` 或 `Ctrl + ,` | 搜索文件、类型、成员和符号 |
| `Ctrl + G` | 跳转到指定行 |
| `Ctrl + Tab` | 在最近打开的文件之间切换 |
| `Ctrl + F6` | 切换到下一个文档 |
| `Ctrl + M, Ctrl + M` | 折叠或展开当前代码块 |
| `Ctrl + M, Ctrl + O` | 折叠到定义级别 |
| `Ctrl + M, Ctrl + L` | 展开全部代码 |

## 重构与代码生成

| 快捷键 | 作用 |
| --- | --- |
| `Ctrl + R, Ctrl + R` | 重命名符号 |
| `Ctrl + .` | 打开快速操作和修复建议 |
| `Ctrl + K, Ctrl + S` | 用代码片段包围选中代码 |
| `Ctrl + K, Ctrl + X` | 插入代码片段 |

例如，选中代码后使用 `Ctrl + K, Ctrl + S`，可以快速添加 `if`、`for` 或 `try` 等结构。

## 编译与运行

| 快捷键 | 作用 |
| --- | --- |
| `Ctrl + Shift + B` | 生成解决方案 |
| `Ctrl + F5` | 启动但不调试 |
| `F5` | 启动调试 |
| `Shift + F5` | 停止调试 |
| `Ctrl + Break` | 中止构建 |
| `Ctrl + Alt + L` | 打开 Solution Explorer |
| `Ctrl + \, Ctrl + E` | 打开 Error List |

## 调试

| 快捷键 | 作用 |
| --- | --- |
| `F9` | 添加或移除断点 |
| `F10` | Step Over：执行当前行，不进入函数 |
| `F11` | Step Into：进入函数 |
| `Shift + F11` | Step Out：跳出当前函数 |
| `Ctrl + F10` | 运行到光标位置 |
| `Shift + F9` | 打开 QuickWatch |
| `Ctrl + Alt + W, 1` | 打开 Watch 1 |
| `Ctrl + Alt + C` | 打开 Call Stack |
| `Ctrl + Alt + O` | 打开 Output 窗口 |

## GLES 和 ANGLE 学习时优先记住

刚开始不必一次记住所有快捷键。优先熟悉下面这些：

| 快捷键 | 使用场景 |
| --- | --- |
| `F12` | 跳进 GLFW、EGL、GLES 或 ANGLE 符号定义 |
| `Alt + F12` | 临时阅读定义，不打断当前位置 |
| `Shift + F12` | 查看一个 API 或变量在哪里被使用 |
| `Ctrl + -` | 阅读源码后快速返回 |
| `Ctrl + T` | 快速查找文件和符号 |
| `Ctrl + .` | 查看 IDE 提供的修复和重构建议 |
| `Ctrl + Shift + B` | 重新生成解决方案 |
| `F9` | 在初始化、shader 编译或 draw call 前设置断点 |
| `F10` | 逐行观察程序状态 |
| `F11` | 进入自己编写的封装函数 |

调试 OpenGL ES Lab 时，可以先在这些调用附近设置断点：

```cpp
glfwCreateWindow(...);
glfwMakeContextCurrent(...);
glCompileShader(...);
glLinkProgram(...);
glBufferData(...);
glDrawElements(...);
```

