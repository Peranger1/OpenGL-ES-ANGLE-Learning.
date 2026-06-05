# EGL 与 OpenGL ES 整体知识架构

## 1. 学习目标

本文用于建立 EGL、OpenGL ES 3.0 和 GLSL ES 的应用层知识框架。

重点不是记忆全部 API，而是能够回答：

```text
窗口如何成为绘制目标？
GLES API 实际操作的是谁的状态？
顶点、纹理和矩阵如何进入 Shader？
一次 Draw Call 会消费哪些数据？
CPU 与 GPU 为什么需要同步？
多线程环境下 Context 和共享资源如何组织？
阅读陌生渲染代码时，应该从哪里入手？
```

ANGLE 是 Windows 平台上的 EGL 和 OpenGL ES 实现之一。应用仍然使用标准 EGL/GLES API。

---

# 2. 全局视图

## 2.1 应用层结构

```text
操作系统窗口
  ↓
EGLDisplay
  ↓
EGLConfig
  ↓
EGLSurface + EGLContext
  ↓ eglMakeCurrent
当前线程获得 GLES 调用环境
  ↓
创建 Buffer、Texture、Framebuffer、Shader Program
  ↓
设置 GLES 状态
  ↓
提交 Draw Call
  ↓
默认 Framebuffer 或自建 FBO
  ↓
eglSwapBuffers
  ↓
窗口显示
```

## 2.2 一帧的基本结构

```cpp
while (running) {
    processInput();
    updateScene();

    glViewport(...);
    glClear(...);

    glUseProgram(program);
    glBindVertexArray(vao);
    glBindTexture(GL_TEXTURE_2D, texture);

    glUniformMatrix4fv(...);
    glDrawElements(...);

    eglSwapBuffers(display, surface);
}
```

需要形成的直觉：

```text
绝大多数 GLES API 在准备资源或修改状态。
glDrawArrays() 和 glDrawElements() 才会提交绘制命令。
eglSwapBuffers() 将窗口后缓冲区提交给窗口系统显示。
```

---

# 3. EGL：连接窗口系统与 GLES

## 3.1 EGL 负责什么？

EGL 不负责绘制三角形。

EGL 负责：

```text
连接 EGL 实现
选择缓冲区配置
创建绘制目标
创建 GLES Context
将 Context 绑定到线程
交换窗口缓冲区
管理多线程 Context
```

ANGLE 提供 `libEGL.dll` 和 `libGLESv2.dll`。  
在 Windows 中，应用可以通过 ANGLE 使用标准 EGL/GLES API。

## 3.2 核心对象

| 对象 | 含义 |
|---|---|
| `EGLDisplay` | 应用与 EGL 实现之间的连接 |
| `EGLConfig` | 描述颜色、深度、模板缓冲区及 Surface 能力 |
| `EGLSurface` | GLES 的绘制目标，例如窗口或离屏缓冲区 |
| `EGLContext` | GLES 状态容器和命令提交环境 |
| `EGLSync` / `GLsync` | 用于表达命令之间的同步关系 |

对象关系：

```text
EGLDisplay
  ├── EGLConfig
  ├── EGLSurface：画到哪里
  └── EGLContext：使用什么 GLES 状态执行命令
```

## 3.3 标准初始化顺序

```cpp
EGLDisplay display =
    eglGetDisplay(EGL_DEFAULT_DISPLAY);

eglInitialize(display, nullptr, nullptr);

eglBindAPI(EGL_OPENGL_ES_API);

eglChooseConfig(
    display,
    configAttributes,
    &config,
    1,
    &configCount
);

EGLSurface surface =
    eglCreateWindowSurface(
        display,
        config,
        nativeWindow,
        nullptr
    );

EGLContext context =
    eglCreateContext(
        display,
        config,
        EGL_NO_CONTEXT,
        contextAttributes
    );

eglMakeCurrent(
    display,
    surface,
    surface,
    context
);
```

## 3.4 `eglMakeCurrent()` 为什么重要？

```cpp
eglMakeCurrent(
    display,
    drawSurface,
    readSurface,
    context
);
```

它将 Context 和 Surface 绑定到当前线程。

此后，当前线程中的 GLES API 才知道：

```text
操作哪个 Context？
读取哪个 Surface？
绘制到哪个 Surface？
```

例如：

```cpp
glBindBuffer(...);
glUseProgram(...);
glDrawElements(...);
```

这些 API 没有显式接收 `context` 参数，因为它们隐式操作当前线程绑定的 Context。

## 3.5 EGL 销毁顺序

```text
删除 GLES 对象
→ eglMakeCurrent 解除 Context 绑定
→ eglDestroyContext
→ eglDestroySurface
→ eglTerminate
→ eglReleaseThread
→ 销毁操作系统窗口
```

示例：

```cpp
glDeleteProgram(program);
glDeleteBuffers(1, &vbo);

eglMakeCurrent(
    display,
    EGL_NO_SURFACE,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT
);

eglDestroyContext(display, context);
eglDestroySurface(display, surface);
eglTerminate(display);
eglReleaseThread();
```

---

# 4. OpenGL ES：基于 Context 的状态机

## 4.1 状态机思维

OpenGL ES 不是“每次绘制都传入完整参数”的 API。

它更接近：

```text
选择当前对象
→ 修改当前对象或当前 Context 的状态
→ 提交 Draw Call
```

例如：

```cpp
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
```

`glBufferData()` 没有接收 `vbo`。  
它操作的是当前绑定到 `GL_ARRAY_BUFFER` 的 Buffer。

## 4.2 常见 API 分类

### 创建与销毁对象

```cpp
glGenBuffers
glDeleteBuffers

glGenTextures
glDeleteTextures

glGenVertexArrays
glDeleteVertexArrays

glGenFramebuffers
glDeleteFramebuffers

glCreateShader
glDeleteShader

glCreateProgram
glDeleteProgram
```

### 绑定当前对象

```cpp
glBindBuffer
glBindTexture
glBindVertexArray
glBindFramebuffer
glUseProgram
```

### 上传或更新数据

```cpp
glBufferData
glBufferSubData

glTexImage2D
glTexSubImage2D

glUniformMatrix4fv
glUniform3fv
glUniform1i
```

### 设置渲染状态

```cpp
glEnable
glDisable

glDepthFunc
glBlendFunc
glCullFace
glViewport
```

### 发起绘制

```cpp
glDrawArrays
glDrawElements
```

### 查询与同步

```cpp
glGetString
glGetIntegerv
glGetError

glFenceSync
glWaitSync
glClientWaitSync
glFlush
glFinish
```

## 4.3 Draw Call 消费什么状态？

调用：

```cpp
glDrawElements(
    GL_TRIANGLES,
    36,
    GL_UNSIGNED_SHORT,
    nullptr
);
```

驱动会综合消费当前 Context 中的状态：

```text
当前 Shader Program
当前 VAO
VAO 关联的 VBO 和 EBO
当前 Texture Unit 上绑定的 Texture
Program 中的 Uniform
深度测试、混合、裁剪等开关
当前 Framebuffer
Viewport
```

阅读代码时，应沿着 Draw Call 向上回溯状态来源。

---

# 5. 顶点数据：VBO、EBO 与 VAO

## 5.1 VBO：保存原始顶点字节

```cpp
glGenBuffers(1, &vbo);
glBindBuffer(GL_ARRAY_BUFFER, vbo);

glBufferData(
    GL_ARRAY_BUFFER,
    sizeof(vertices),
    vertices,
    GL_STATIC_DRAW
);
```

VBO 保存原始字节。它本身不知道：

```text
每个顶点有多少字节？
哪些字节代表坐标？
哪些字节代表颜色？
哪些字节代表法线？
```

## 5.2 VAO：描述如何读取顶点数据

假设每个顶点为：

```cpp
struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
};
```

VAO 配置：

```cpp
glBindVertexArray(vao);
glBindBuffer(GL_ARRAY_BUFFER, vbo);

glVertexAttribPointer(
    0,
    3,
    GL_FLOAT,
    GL_FALSE,
    sizeof(Vertex),
    reinterpret_cast<void*>(offsetof(Vertex, position))
);
glEnableVertexAttribArray(0);
```

参数含义：

```text
location = 0
每次读取 3 个 float
相邻顶点间隔 sizeof(Vertex)
从 position 字段偏移开始读取
```

VAO 会记住顶点属性布局以及对应 Buffer。

## 5.3 EBO：保存索引

```cpp
glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

glBufferData(
    GL_ELEMENT_ARRAY_BUFFER,
    sizeof(indices),
    indices,
    GL_STATIC_DRAW
);
```

索引绘制：

```cpp
glDrawElements(
    GL_TRIANGLES,
    6,
    GL_UNSIGNED_SHORT,
    nullptr
);
```

含义：

```text
从当前 EBO 读取 6 个索引
每个索引类型为 unsigned short
每 3 个索引组成一个三角形
```

## 5.4 三者关系

```text
VBO：原始顶点字节
EBO：顶点读取顺序
VAO：顶点字节的解释规则，以及关联的 Buffer
```

---

# 6. Shader 渲染管线

## 6.1 基本流程

```text
VBO 中的顶点属性
→ Vertex Shader
→ 图元组装
→ 光栅化
→ 插值
→ Fragment Shader
→ 深度测试、混合等测试
→ Framebuffer
```

## 6.2 Vertex Shader

示例：

```glsl
#version 300 es

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldNormal;
out vec2 vTexCoord;

void main() {
    vWorldNormal = mat3(uModel) * aNormal;
    vTexCoord = aTexCoord;

    gl_Position =
        uProjection *
        uView *
        uModel *
        vec4(aPosition, 1.0);
}
```

职责：

```text
接收单个顶点属性
执行坐标变换
计算需要传递给 Fragment Shader 的数据
写入 gl_Position
```

`gl_Position` 是必须写入的内置变量，表示裁剪空间位置。

## 6.3 光栅化与插值

三角形的三个顶点可能输出三种不同颜色：

```glsl
out vec3 vColor;
```

光栅化阶段会为三角形内部的每个片元插值：

```text
顶点 A：红色
顶点 B：绿色
顶点 C：蓝色
→ 三角形内部形成平滑渐变
```

Fragment Shader 接收到的是插值后的值。

## 6.4 Fragment Shader

示例：

```glsl
#version 300 es
precision mediump float;

in vec3 vWorldNormal;
in vec2 vTexCoord;

uniform sampler2D uTexture;

out vec4 fragColor;

void main() {
    vec3 normal = normalize(vWorldNormal);
    vec4 color = texture(uTexture, vTexCoord);

    fragColor = color;
}
```

职责：

```text
接收插值后的数据
读取纹理
计算颜色
写入输出颜色
```

## 6.5 Shader 变量来源

| GLSL 类型 | 来源 | 更新频率 |
|---|---|---|
| `in` 顶点属性 | VBO，由 VAO 描述 | 每个顶点 |
| `uniform` | CPU 通过 GLES API 上传 | 每帧、每个物体或每种材质 |
| Vertex Shader `out` | Vertex Shader 计算 | 每个顶点 |
| Fragment Shader `in` | 光栅化阶段插值 | 每个片元 |
| `sampler2D` | CPU 指定纹理单元 | 通常按材质设置 |
| `fragColor` | Fragment Shader 输出 | 每个片元 |

## 6.6 阅读 Shader 的固定顺序

阅读陌生 Shader 时，依次回答：

```text
1. Shader 属于哪个阶段？
2. 每个 in 从哪里来？
3. 每个 uniform 由 CPU 在哪里上传？
4. 顶点处于哪个坐标空间？
5. Vertex Shader 输出了什么？
6. 哪些数据经过插值？
7. Fragment Shader 读取了哪些 Texture？
8. 最终输出写入哪个 Framebuffer？
```

---

# 7. 坐标空间与矩阵

## 7.1 常见坐标空间

```text
模型空间
→ 世界空间
→ 观察空间
→ 裁剪空间
→ NDC
→ 屏幕空间
```

## 7.2 MVP 矩阵

```glsl
gl_Position =
    uProjection *
    uView *
    uModel *
    vec4(aPosition, 1.0);
```

| 矩阵 | 作用 |
|---|---|
| `Model` | 将模型放入世界空间 |
| `View` | 表达相机位置和朝向 |
| `Projection` | 应用透视投影或正交投影 |

CPU 侧常用 GLM：

```cpp
glm::mat4 model(1.0f);

model = glm::translate(
    model,
    glm::vec3(0.0f, 0.0f, -3.0f)
);

glm::mat4 view = glm::lookAt(
    cameraPosition,
    target,
    upDirection
);

glm::mat4 projection = glm::perspective(
    glm::radians(60.0f),
    aspectRatio,
    0.1f,
    100.0f
);
```

## 7.3 法线矩阵

如果模型存在非等比缩放，法线不能直接乘以 Model 矩阵。

CPU 侧计算：

```cpp
glm::mat3 normalMatrix =
    glm::transpose(
        glm::inverse(glm::mat3(model))
    );
```

Shader 中使用：

```glsl
vec3 worldNormal =
    normalize(uNormalMatrix * aNormal);
```

---

# 8. Texture 与 Texture Unit

## 8.1 创建 Texture

```cpp
glGenTextures(1, &texture);
glBindTexture(GL_TEXTURE_2D, texture);

glTexParameteri(
    GL_TEXTURE_2D,
    GL_TEXTURE_MIN_FILTER,
    GL_LINEAR
);

glTexParameteri(
    GL_TEXTURE_2D,
    GL_TEXTURE_MAG_FILTER,
    GL_LINEAR
);

glTexImage2D(
    GL_TEXTURE_2D,
    0,
    GL_RGBA,
    width,
    height,
    0,
    GL_RGBA,
    GL_UNSIGNED_BYTE,
    pixels
);
```

## 8.2 Texture Object 与 Texture Unit

Texture Object：

```text
保存图像、尺寸、格式和采样参数
```

Texture Unit：

```text
Shader 访问纹理时使用的绑定槽位
```

绑定流程：

```cpp
glActiveTexture(GL_TEXTURE0);
glBindTexture(GL_TEXTURE_2D, texture);

glUseProgram(program);

glUniform1i(
    glGetUniformLocation(program, "uTexture"),
    0
);
```

Shader：

```glsl
uniform sampler2D uTexture;

vec4 color = texture(uTexture, vTexCoord);
```

关键理解：

```text
sampler2D Uniform 保存纹理单元编号。
它不保存 Texture Object ID。
```

---

# 9. Framebuffer 与离屏渲染

## 9.1 默认 Framebuffer

创建窗口 Surface 后，可以直接绘制：

```cpp
glBindFramebuffer(GL_FRAMEBUFFER, 0);
glDrawElements(...);
eglSwapBuffers(display, surface);
```

`0` 表示默认 Framebuffer，通常对应窗口后缓冲区。

## 9.2 自建 FBO

```cpp
glGenFramebuffers(1, &fbo);
glBindFramebuffer(GL_FRAMEBUFFER, fbo);

glFramebufferTexture2D(
    GL_FRAMEBUFFER,
    GL_COLOR_ATTACHMENT0,
    GL_TEXTURE_2D,
    colorTexture,
    0
);
```

FBO 本身不保存像素。  
它描述输出应写入哪些 Attachment。

## 9.3 后处理流程

```text
第一遍绘制
场景
→ 自建 FBO
→ Color Texture

第二遍绘制
Color Texture
→ 全屏矩形
→ 后处理 Fragment Shader
→ 默认 Framebuffer
→ 窗口
```

常见后处理：

```text
灰度
模糊
锐化
色调映射
抗锯齿
Bloom
```

---

# 10. 深度、混合与裁剪

## 10.1 深度测试

```cpp
glEnable(GL_DEPTH_TEST);

glClear(
    GL_COLOR_BUFFER_BIT |
    GL_DEPTH_BUFFER_BIT
);
```

作用：

```text
较近片元覆盖较远片元
```

## 10.2 Alpha 混合

```cpp
glEnable(GL_BLEND);

glBlendFunc(
    GL_SRC_ALPHA,
    GL_ONE_MINUS_SRC_ALPHA
);
```

常见透明绘制顺序：

```text
先绘制不透明物体
→ 再从远到近绘制透明物体
```

## 10.3 背面裁剪

```cpp
glEnable(GL_CULL_FACE);
glCullFace(GL_BACK);
```

作用：

```text
跳过朝向背面的三角形
减少无意义片元处理
```

---

# 11. 渲染循环架构

## 11.1 推荐职责拆分

```text
Platform
  创建窗口
  创建 EGL 对象
  处理输入和窗口消息

Renderer
  管理 GLES 对象
  设置状态
  提交 Draw Call

Scene
  保存 CPU 侧业务数据
  保存物体、相机和灯光

Resource Manager
  加载模型
  解码图片
  管理 Texture、Mesh 和 Program
```

## 11.2 生命周期

```text
初始化
→ 创建窗口与 Context
→ 创建 Renderer
→ 上传资源
→ 创建 Scene
→ 主循环
→ 释放 GLES 资源
→ 销毁 Context
→ 销毁窗口
```

## 11.3 每帧阶段

```text
处理输入
→ 更新 CPU 侧业务状态
→ 计算矩阵
→ 设置 Frame Uniform
→ 按材质和 Mesh 分组
→ 设置 Object Uniform
→ 提交 Draw Call
→ Present
```

## 11.4 数据更新频率

| 数据 | 示例 | 更新频率 |
|---|---|---|
| 静态资源 | Mesh、静态 Texture | 初始化时 |
| Frame 数据 | View、Projection、灯光 | 每帧 |
| Object 数据 | Model、颜色 | 每个物体 |
| Material 数据 | Texture、粗糙度 | 每种材质 |
| Streaming 数据 | 视频帧、摄像头纹理 | 按数据到达频率 |

---

# 12. CPU、驱动与 GPU

## 12.1 不要把 GLES 调用理解为立即执行

调用：

```cpp
glDrawElements(...);
```

通常不代表 GPU 已经完成绘制。

更准确的理解：

```text
应用线程调用 GLES API
→ 驱动验证参数并记录命令
→ 驱动将命令提交给 GPU
→ GPU 异步执行
```

CPU 和 GPU 通常并行工作。

## 12.2 常见误区

错误直觉：

```text
glTexImage2D 返回
→ GPU 一定已经上传完成
```

更准确的理解：

```text
glTexImage2D 返回
→ 驱动已经接收调用
→ 具体执行时机由实现决定
```

除非程序显式同步，否则不要假设 GPU 已完成操作。

---

# 13. 多线程 Context 模型

## 13.1 基本规则

```text
一个线程最多绑定一个 OpenGL ES Context。
一个 Context 同一时间不能绑定到多个线程。
调用 GLES API 前，线程必须拥有 Current Context。
```

绑定：

```cpp
eglMakeCurrent(
    display,
    drawSurface,
    readSurface,
    context
);
```

解除绑定：

```cpp
eglMakeCurrent(
    display,
    EGL_NO_SURFACE,
    EGL_NO_SURFACE,
    EGL_NO_CONTEXT
);
```

## 13.2 推荐的基础模型

多数应用首先采用：

```text
工作线程
  解码图片
  解析模型
  准备 CPU 数据

渲染线程
  持有唯一 GLES Context
  上传 GPU 资源
  提交 Draw Call
  eglSwapBuffers
```

这是最容易维护的模型。

## 13.3 共享 Context 模型

当上传量较大时，可以使用：

```text
渲染线程
  renderContext + windowSurface
  绘制画面

上传线程
  uploadContext + pbufferSurface
  上传 Texture 或 Buffer
```

创建共享 Context：

```cpp
EGLContext uploadContext =
    eglCreateContext(
        display,
        config,
        renderContext,
        contextAttributes
    );
```

第三个参数 `renderContext` 表示共享资源。

## 13.4 哪些对象可以共享？

| 对象 | 是否可以共享 |
|---|---|
| Texture | 是 |
| Buffer | 是 |
| Shader | 是 |
| Program | 是 |
| Renderbuffer | 是 |
| Sampler | 是 |
| Sync | 是 |
| VAO | 否 |
| FBO | 否 |
| Query | 否 |
| Context 中的绑定状态 | 否 |

重要结论：

```text
共享资源不等于共享状态。
```

即使两个 Context 共享 Texture，每个 Context 仍然需要自行：

```cpp
glActiveTexture(GL_TEXTURE0);
glBindTexture(GL_TEXTURE_2D, texture);
```

---

# 14. CPU 与 GPU 同步

## 14.1 两种完全不同的同步

CPU 线程同步：

```cpp
std::mutex
std::condition_variable
std::atomic
```

作用：

```text
保护 C++ 数据结构
传递槽位状态
协调工作线程
```

GPU 命令同步：

```cpp
glFenceSync
glWaitSync
glClientWaitSync
glFlush
glFinish
```

作用：

```text
表达 GPU 命令之间的依赖关系
避免读取尚未上传完成的资源
避免覆盖 GPU 仍在使用的资源
```

二者不能互相替代。

## 14.2 Fence

```cpp
GLsync fence =
    glFenceSync(
        GL_SYNC_GPU_COMMANDS_COMPLETE,
        0
    );
```

Fence 插入到当前 Context 的命令流中。

语义：

```text
当 Fence 被触发时，
它之前提交的 GPU 命令已经完成。
```

## 14.3 `glFlush()`

```cpp
glFlush();
```

作用：

```text
要求驱动将当前 Context 中积累的命令推进执行。
```

它不会等待 GPU 完成。

适合：

```text
跨 Context 同步
Fence 后及时提交
```

## 14.4 `glWaitSync()`

```cpp
glWaitSync(
    fence,
    0,
    GL_TIMEOUT_IGNORED
);
```

作用：

```text
让 GPU 命令流等待 Fence。
通常不会阻塞 CPU。
```

适合：

```text
渲染线程等待上传 Context 完成纹理更新
```

## 14.5 `glClientWaitSync()`

```cpp
glClientWaitSync(
    fence,
    0,
    timeout
);
```

作用：

```text
让 CPU 线程等待或轮询 Fence。
```

适合：

```text
CPU 准备复用资源前，
确认 GPU 已不再访问它。
```

## 14.6 `glFinish()`

```cpp
glFinish();
```

作用：

```text
阻塞 CPU，
直到当前 Context 之前的命令全部完成。
```

适合：

```text
简单验证
退出前清理
调试同步问题
```

不适合在每帧频繁调用，否则会破坏 CPU 与 GPU 并行。

---

# 15. 流式纹理双缓冲

## 15.1 使用场景

```text
视频播放
摄像头画面
远程桌面
动态图像
持续变化的数据可视化
```

## 15.2 双缓冲槽位状态

```text
Free
→ Uploading
→ Ready
→ Displaying
→ Free
```

## 15.3 数据流

```text
上传线程
CPU 生成或解码像素
→ glTexSubImage2D
→ glFenceSync
→ glFlush
→ 标记 Ready

渲染线程
发现 Ready
→ glWaitSync
→ glBindTexture
→ glDrawElements
→ 插入 reuseFence
→ 旧纹理标记 Free
```

## 15.4 为什么需要两个 Fence？

上传 Fence：

```text
防止渲染线程读取尚未上传完成的新纹理
```

复用 Fence：

```text
防止上传线程覆盖 GPU 仍在读取的旧纹理
```

---

# 16. ANGLE 在知识架构中的位置

## 16.1 ANGLE 的作用

```text
应用调用 EGL / OpenGL ES
→ ANGLE 接收 API 调用
→ ANGLE 将命令映射至平台图形 API
→ D3D11、Vulkan、桌面 OpenGL 或其他后端
```

## 16.2 当前学习边界

现阶段需要掌握：

```text
标准 EGL 生命周期
标准 GLES 状态机
Shader 数据流
资源生命周期
多线程 Context
同步模型
```

暂时不必深入：

```text
ANGLE 内部 Renderer 抽象
ANGLE Dirty Bits
D3D11 与 Vulkan 后端实现
Shader Translator 内部 AST
```

完成应用层框架后，再阅读 ANGLE 源码会更自然。

---

# 17. 阅读陌生 GLES 项目的方法

## 17.1 第一步：找到 Context 初始化

搜索：

```text
eglInitialize
eglCreateContext
eglMakeCurrent
glfwMakeContextCurrent
```

回答：

```text
Context 在哪里创建？
绑定到哪个线程？
使用 GLES 2.0、3.0 还是 3.1？
Surface 是窗口还是离屏缓冲区？
```

## 17.2 第二步：找到主循环

搜索：

```text
eglSwapBuffers
glfwSwapBuffers
render
draw
present
```

回答：

```text
每帧在哪里开始？
输入何时处理？
业务状态何时更新？
Draw Call 在哪里提交？
```

## 17.3 第三步：从 Draw Call 向上回溯

搜索：

```text
glDrawArrays
glDrawElements
```

向上查找：

```text
绑定了哪个 Program？
绑定了哪个 VAO？
使用了哪些 Texture？
上传了哪些 Uniform？
绘制到哪个 Framebuffer？
开启了哪些渲染状态？
```

## 17.4 第四步：阅读 Shader

逐项记录：

```text
Vertex Shader 输入
Vertex Shader 输出
Uniform
Texture
坐标空间
Fragment Shader 输出
```

## 17.5 第五步：寻找资源生命周期

搜索：

```text
glGen*
glCreate*
glDelete*
```

确认：

```text
资源在哪里创建？
资源是否重复创建？
资源在哪里释放？
释放时 Context 是否仍然有效？
```

## 17.6 第六步：检查线程模型

搜索：

```text
std::thread
eglMakeCurrent
glFenceSync
glWaitSync
glClientWaitSync
glFlush
glFinish
```

回答：

```text
哪个线程拥有渲染 Context？
是否存在共享 Context？
共享了哪些资源？
CPU 同步和 GPU 同步是否完整？
```

---

# 18. API 速查表

## EGL

```text
eglGetDisplay             获取 EGLDisplay
eglInitialize             初始化 EGL
eglBindAPI                选择 OpenGL ES API
eglChooseConfig           选择缓冲区配置
eglCreateWindowSurface    创建窗口 Surface
eglCreatePbufferSurface   创建离屏 Surface
eglCreateContext          创建 Context
eglMakeCurrent            将 Context 绑定到线程
eglSwapBuffers            显示窗口后缓冲区
eglDestroyContext         销毁 Context
eglDestroySurface         销毁 Surface
eglTerminate              终止 EGLDisplay
```

## Buffer 与顶点

```text
glGenBuffers
glBindBuffer
glBufferData
glBufferSubData

glGenVertexArrays
glBindVertexArray
glVertexAttribPointer
glEnableVertexAttribArray
```

## Shader

```text
glCreateShader
glShaderSource
glCompileShader
glGetShaderiv
glGetShaderInfoLog

glCreateProgram
glAttachShader
glLinkProgram
glUseProgram

glGetUniformLocation
glUniform*
```

## Texture

```text
glGenTextures
glBindTexture
glActiveTexture
glTexParameteri
glTexImage2D
glTexSubImage2D
```

## Framebuffer

```text
glGenFramebuffers
glBindFramebuffer
glFramebufferTexture2D
glCheckFramebufferStatus
```

## 绘制

```text
glViewport
glClearColor
glClear

glEnable
glDisable

glDrawArrays
glDrawElements
```

## 同步

```text
glFenceSync
glWaitSync
glClientWaitSync
glFlush
glFinish
```

---

# 19. 进阶实验 15-32

这组实验的目标不是继续“画更多东西”，而是把你脑子里的 GLES 使用框架补完整：状态如何控制渲染结果，资源如何进入 GPU，离屏渲染如何组织，EGL 如何选择运行环境，以及 GPU 管线里哪些阶段可以被查询、缓存或复用。

## 19.1 15-render-states：渲染状态

核心问题：

- 为什么同一批顶点，在不同状态下会得到完全不同的结果？
- 哪些状态属于全局状态，哪些状态挂在 VAO、FBO、Program、Texture 上？
- Draw Call 真正消费的是“当前上下文里的状态集合”。

重点 API：

```text
glEnable / glDisable
glCullFace
glFrontFace
glDepthFunc
glDepthMask
glBlendFunc
glBlendEquation
glColorMask
glStencilFunc
glStencilOp
glScissor
glViewport
```

你应该能从代码里读出：

- 是否开启深度测试、背面剔除、混合、模板测试、裁剪测试。
- 半透明物体为什么通常要关闭深度写入或按远近排序。
- `glViewport()` 和 `glScissor()` 都影响区域，但一个控制 NDC 到窗口坐标变换，一个控制片元是否被裁剪。

## 19.2 16-texture-sampler-mipmap：纹理采样

核心问题：

- Texture Object 保存图像数据。
- Texture Unit 是 shader 访问纹理的槽位。
- Sampler 参数决定如何从纹理中取值。
- Mipmap 解决远处纹理采样闪烁和性能问题。

重点 API：

```text
glActiveTexture
glBindTexture
glTexImage2D
glTexSubImage2D
glTexParameteri
glGenerateMipmap
glUniform1i
```

阅读代码时重点看三条线：

```text
CPU 图像数据
→ glTexImage2D 上传到 Texture Object
→ glActiveTexture + glBindTexture 绑定到 Texture Unit
→ sampler2D uniform 指向某个 Texture Unit
→ fragment shader texture(...) 采样
```

## 19.3 17-cubemap：立方体贴图

核心问题：

- Cube Map 是 6 张 2D 纹理组成的方向采样纹理。
- `texture(samplerCube, direction)` 不是用 uv，而是用 3D 方向向量采样。
- 常用于 skybox、环境反射、点光源阴影。

重点 API：

```text
glBindTexture(GL_TEXTURE_CUBE_MAP)
glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, ...)
glTexParameteri(GL_TEXTURE_CUBE_MAP, ...)
```

关键理解：

- Skybox 通常只保留相机旋转，去掉相机平移。
- Skybox 常配合 `glDepthFunc(GL_LEQUAL)`，让背景在深度为 1 的边界上仍能通过。

## 19.4 18-framebuffer-resize：窗口变化与 FBO 重建

核心问题：

- 默认 framebuffer 会跟窗口大小变化。
- 自己创建的 FBO 附件不会自动变化，需要手动重建或重新分配。

重点 API：

```text
glViewport
glTexImage2D
glRenderbufferStorage
glFramebufferTexture2D
glFramebufferRenderbuffer
glCheckFramebufferStatus
```

推荐记住这个流程：

```text
窗口尺寸变化
→ 更新 viewport
→ 重新分配离屏 color texture
→ 重新分配 depth/stencil renderbuffer
→ 重新检查 FBO completeness
→ 后处理阶段使用新的纹理尺寸
```

## 19.5 19-mrt-msaa：多渲染目标与多重采样

核心问题：

- MRT 让一个 fragment shader 同时输出多个 color attachment。
- MSAA 让每个像素保存多个 sample，最后 resolve 成普通纹理或默认 framebuffer。

重点 API：

```text
glDrawBuffers
glRenderbufferStorageMultisample
glBlitFramebuffer
glReadBuffer
glFramebufferTexture2D
glFramebufferRenderbuffer
```

GLES 3.0 里需要特别注意：

```text
glDrawBuffers(2, { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 })
```

如果只想写 `COLOR_ATTACHMENT1`，第 0 个位置不能直接省略，应该写：

```text
glDrawBuffers(2, { GL_NONE, GL_COLOR_ATTACHMENT1 })
```

否则会遇到类似：

```text
glDrawBuffers: Ith value does not match COLOR_ATTACHMENTi or NONE
```

## 19.6 20-uniform-buffer：Uniform Buffer Object

核心问题：

- 普通 uniform 适合少量、零散参数。
- UBO 适合一组多个 shader 共用的常量数据，例如相机矩阵、光源参数、材质参数。

重点 API：

```text
glGetUniformBlockIndex
glUniformBlockBinding
glBindBufferBase
glBufferData
glBufferSubData
```

数据流：

```text
C++ struct / packed float data
→ glBufferData 创建 UBO
→ glBindBufferBase 绑定到 binding point
→ shader uniform block 通过 layout(std140) 读取
```

阅读 UBO 代码时，最重要的是确认：

- shader 里的 `layout(std140)`。
- C++ 侧数据布局是否符合 std140 对齐规则。
- block binding 与 `glBindBufferBase()` 的 index 是否一致。

## 19.7 21-instanced-cubes：实例化渲染

核心问题：

- 多个物体共享同一份网格，只是每个实例的 transform 或颜色不同。
- Instancing 用一次 draw call 绘制多个实例，减少 CPU 提交成本。

重点 API：

```text
glVertexAttribDivisor
glDrawArraysInstanced
glDrawElementsInstanced
```

理解方式：

```text
普通 attribute：每个顶点前进一次
instance attribute：每个实例前进一次
```

典型用途：

- 粒子
- 草地、石头、树
- 大量重复小物件
- debug draw 中的大批量 marker

## 19.8 22-dynamic-buffer-streaming：动态缓冲更新

核心问题：

- 每帧变化的顶点数据不适合长期静态上传。
- 动态更新要避免 CPU 等 GPU 用完旧数据。

重点 API：

```text
glBufferData
glBufferSubData
glMapBufferRange
glUnmapBuffer
glFenceSync
glClientWaitSync
```

常见策略：

```text
orphaning:
glBufferData(target, size, nullptr, GL_STREAM_DRAW)
glBufferSubData(target, 0, size, data)

mapping:
glMapBufferRange(target, offset, size, flags)
memcpy(...)
glUnmapBuffer(target)

ring buffer:
每帧写不同 offset，用 fence 判断旧区域是否安全复用
```

如果本地 GLES 头文件没有 `GL_MIN_MAP_BUFFER_ALIGNMENT`，说明该枚举不在当前 GLES 3.0 头里；学习主线可以先跳过这个查询，不影响 `glMapBufferRange()` 的核心理解。

## 19.9 23-pixel-buffer-transfer：像素缓冲传输

核心问题：

- PBO 是给像素上传或读取使用的 Buffer Object。
- 它能把 CPU 内存拷贝和 GPU 纹理上传/读取拆开，便于异步化。

重点 API：

```text
glBindBuffer(GL_PIXEL_UNPACK_BUFFER, ...)
glBindBuffer(GL_PIXEL_PACK_BUFFER, ...)
glTexSubImage2D
glReadPixels
glMapBufferRange
```

两条数据流：

```text
上传:
CPU 写入 PBO
→ glTexSubImage2D 从 PBO 当前 offset 读取
→ Texture 更新

读取:
glReadPixels 写入 PBO
→ 之后某一帧 map PBO
→ CPU 读取结果
```

## 19.10 24-shader-interfaces：Shader 接口

核心问题：

- Vertex Shader 输出变量和 Fragment Shader 输入变量要匹配。
- Uniform 在多个 shader stage 中如果同名，类型和精度也要匹配。

重点内容：

```glsl
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;

out mediump vec2 vUV;

uniform mediump float uTime;
```

常见错误：

```text
Precisions of uniform 'uTime' differ between VERTEX and FRAGMENT shaders.
```

解决原则：

- 同名 uniform 在 vertex/fragment shader 中使用相同类型。
- 在 GLES shader 中显式写出 precision，尤其是 fragment shader。
- varying/in-out 的类型、数组大小、结构体字段保持一致。

## 19.11 25-shadow-map：阴影贴图

核心问题：

- 从光源视角渲染深度图。
- 从相机视角渲染场景时，把世界坐标转换到光源裁剪空间，查询深度图判断是否被遮挡。

重点 API：

```text
glFramebufferTexture2D
glTexImage2D(... depth format ...)
glViewport
glClear(GL_DEPTH_BUFFER_BIT)
```

数据流：

```text
pass 1:
scene geometry
→ light MVP
→ depth texture

pass 2:
scene geometry
→ camera MVP
→ fragment world/light space position
→ sample shadow map
→ compare current depth and stored depth
```

容易出问题的点：

- depth bias 太小会出现 shadow acne。
- depth bias 太大会出现 peter panning。
- shadow map 分辨率和 light projection 范围都会影响阴影质量。

## 19.12 26-capabilities-report：能力查询

核心问题：

- GLES 代码不能假设所有设备能力都一样。
- 程序启动时可以输出能力报告，判断是否支持某些路径。

重点 API：

```text
glGetString
glGetIntegerv
glGetStringi
eglQueryString
```

推荐检查：

```text
GL_VERSION
GL_RENDERER
GL_VENDOR
GL_SHADING_LANGUAGE_VERSION
GL_MAX_TEXTURE_SIZE
GL_MAX_VERTEX_ATTRIBS
GL_MAX_COLOR_ATTACHMENTS
GL_MAX_DRAW_BUFFERS
GL_MAX_SAMPLES
GL_EXTENSIONS
EGL_VERSION
EGL_EXTENSIONS
```

## 19.13 27-egl-config-explorer：EGL Config 选择

核心问题：

- `eglChooseConfig()` 不是创建配置，而是从平台支持的配置列表里筛选。
- Config 决定 color/depth/stencil buffer 格式、surface 类型、renderable 类型等。

重点 API：

```text
eglGetConfigs
eglChooseConfig
eglGetConfigAttrib
```

重要属性：

```text
EGL_RED_SIZE / EGL_GREEN_SIZE / EGL_BLUE_SIZE / EGL_ALPHA_SIZE
EGL_DEPTH_SIZE
EGL_STENCIL_SIZE
EGL_SURFACE_TYPE
EGL_RENDERABLE_TYPE
EGL_SAMPLE_BUFFERS
EGL_SAMPLES
```

读别人代码时，要重点确认它要的是：

- window surface 还是 pbuffer surface。
- GLES2 还是 GLES3。
- 是否需要 depth/stencil。
- 是否需要 MSAA。

## 19.14 28-egl-pbuffer：离屏 EGL Surface

核心问题：

- Pbuffer 是不依赖窗口的离屏 surface。
- 很适合做 headless 测试、离屏渲染、资源预处理、小型验证程序。

重点 API：

```text
eglCreatePbufferSurface
eglMakeCurrent
eglSwapBuffers
eglDestroySurface
```

理解方式：

```text
Window Surface:
渲染结果面向窗口，通常需要和原生窗口系统交互。

Pbuffer Surface:
渲染结果在离屏缓冲中，不需要真实窗口。
```

## 19.15 29-context-recovery：上下文丢失与恢复

核心问题：

- 移动端、浏览器、ANGLE 或驱动场景下，Context 可能丢失。
- 丢失后 GL object 名字不再可靠，资源需要重新创建。

重点 API：

```text
glGetGraphicsResetStatus
eglDestroyContext
eglCreateContext
eglMakeCurrent
```

恢复框架：

```text
检测 context lost
→ 停止使用旧 GL object
→ 销毁或解绑旧 context/surface
→ 创建新 context
→ 重新创建 shader/program/buffer/texture/fbo
→ 恢复 CPU 侧保存的资源数据
```

因此，工程上要区分：

```text
CPU asset data：图片、mesh、shader source，可以长期保存。
GPU object：texture/buffer/program/fbo，context 丢失后需要重建。
```

## 19.16 30-occlusion-query：遮挡查询

核心问题：

- Query 可以询问 GPU 某段绘制是否产生了可见 sample。
- 适合做可见性判断，但读取结果太早会造成 CPU/GPU 同步等待。

重点 API：

```text
glGenQueries
glBeginQuery(GL_ANY_SAMPLES_PASSED, ...)
glEndQuery(GL_ANY_SAMPLES_PASSED)
glGetQueryObjectuiv(... GL_QUERY_RESULT_AVAILABLE ...)
glGetQueryObjectuiv(... GL_QUERY_RESULT ...)
```

如果当前 GLES 头文件没有 `GL_QUERY_COUNTER_BITS`，可以跳过该能力查询；遮挡查询的主线是 `GL_ANY_SAMPLES_PASSED`。

推荐思路：

```text
frame N 发起 query
frame N + 1 或更晚检查 result available
有结果再读取 result
```

## 19.17 31-transform-feedback：Transform Feedback

核心问题：

- Transform Feedback 可以把 vertex shader 的输出写回 buffer。
- 它让 GPU 生成或更新一批顶点数据，不必回到 CPU。

重点 API：

```text
glTransformFeedbackVaryings
glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, ...)
glBeginTransformFeedback
glEndTransformFeedback
glDrawArrays
```

数据流：

```text
input VBO
→ vertex shader
→ selected out variables
→ transform feedback buffer
→ 下一次作为 VBO 使用
```

典型用途：

- GPU 粒子更新
- 顶点变形缓存
- 简单几何数据预处理
- 避免 CPU 读回中间结果

## 19.18 32-program-binary-cache：Program Binary 缓存

核心问题：

- shader 编译和 program link 可能很慢。
- 如果驱动支持 program binary，可以把链接后的 program binary 保存起来，下次直接加载。

重点 API：

```text
glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, ...)
glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE)
glGetProgramBinary
glProgramBinary
```

工程注意事项：

- binary 格式和驱动、GPU、ANGLE 后端、版本强相关。
- 缓存 key 应该包含 shader source hash、GL_RENDERER、GL_VERSION、binary format 等信息。
- binary 加载失败时必须回退到 shader source 编译链接路径。

推荐缓存逻辑：

```text
查找本地 binary cache
→ 尝试 glProgramBinary
→ link status 成功：直接使用
→ 失败：源码编译 link
→ 如果支持 binary retrieval：保存新 binary
```

---

# 20. 学习完成检查清单

## EGL

- [ ] 能够独立写出 EGL 初始化流程
- [ ] 能够解释 Display、Config、Surface 和 Context
- [ ] 理解 `eglMakeCurrent()` 与线程的关系
- [ ] 能够按正确顺序销毁 EGL 对象
- [ ] 能够根据需求筛选 EGL Config
- [ ] 能够使用 Pbuffer 做离屏 EGL 验证
- [ ] 理解 Context 丢失后资源为什么需要重建

## GLES 状态机

- [ ] 能够解释 `glBind*()` 为什么影响后续调用
- [ ] 能够解释 Draw Call 消费哪些状态
- [ ] 能够区分 VBO、EBO 与 VAO
- [ ] 能够区分 Texture Object 与 Texture Unit
- [ ] 能够使用 FBO 实现离屏渲染
- [ ] 能够解释深度、剔除、混合、模板、裁剪等渲染状态
- [ ] 能够解释 MRT、MSAA、Resolve 的关系

## Shader

- [ ] 能够阅读 Vertex Shader 与 Fragment Shader
- [ ] 能够区分 Attribute、Uniform 和插值变量
- [ ] 能够解释 MVP 矩阵
- [ ] 能够解释纹理采样数据流
- [ ] 能够解释基础漫反射和法线用途
- [ ] 能够检查 shader stage 之间的 in/out 接口
- [ ] 能够处理 uniform 类型和 precision 不一致的问题

## 渲染循环

- [ ] 能够拆分初始化、更新、渲染和清理阶段
- [ ] 能够区分 Frame、Object 与 Material 数据
- [ ] 能够从 Draw Call 反向追踪状态来源
- [ ] 能够用 UBO 管理帧级或材质级常量
- [ ] 能够用 Instancing 减少重复物体的 draw call

## 多线程

- [ ] 理解 Context 不能同时绑定到多个线程
- [ ] 能够创建共享 Context
- [ ] 能够列出可共享与不可共享对象
- [ ] 能够区分 CPU 同步与 GPU 同步
- [ ] 能够解释 Fence、Flush、Wait 和 Finish
- [ ] 能够解释动态 buffer、PBO、Transform Feedback 的数据流方向
- [ ] 能够解释 Query 和 Program Binary Cache 的适用场景

---

# 21. 推荐实验顺序

```text
01 最小三角形
→ 02 索引矩形
→ 03 Uniform 变换
→ 04 Texture
→ 05 深度测试与立方体
→ 06 FBO 后处理
→ 07 原生 EGL
→ 09 EGL 生命周期
→ 10 GLES 状态机
→ 11 Shader 光照
→ 12 渲染循环架构
→ 13 多线程共享 Context
→ 14 同步与流式纹理
→ 15 渲染状态
→ 16 纹理采样、Sampler 与 Mipmap
→ 17 Cube Map
→ 18 FBO 尺寸变化
→ 19 MRT 与 MSAA
→ 20 Uniform Buffer Object
→ 21 Instanced Rendering
→ 22 Dynamic Buffer Streaming
→ 23 Pixel Buffer Transfer
→ 24 Shader Interfaces
→ 25 Shadow Map
→ 26 Capabilities Report
→ 27 EGL Config Explorer
→ 28 EGL Pbuffer
→ 29 Context Recovery
→ 30 Occlusion Query
→ 31 Transform Feedback
→ 32 Program Binary Cache
```

`08-angle-backends` 可以独立穿插：

```text
D3D11
D3D11 WARP
Vulkan
Vulkan SwiftShader
Desktop OpenGL
```

---

# 22. 官方参考资料

## EGL

- [EGL 1.5 Specification](https://registry.khronos.org/EGL/specs/eglspec.1.5.pdf)

## OpenGL ES

- [OpenGL ES 3.0 Specification](https://registry.khronos.org/OpenGL/specs/es/3.0/es_spec_3.0.pdf)
- [OpenGL ES 3.0 API Reference](https://registry.khronos.org/OpenGL-Refpages/es3.0/)

## GLSL ES

- [GLSL ES 3.00 Specification](https://registry.khronos.org/OpenGL/specs/es/3.0/GLSL_ES_Specification_3.00.pdf)

## ANGLE

- [ANGLE Repository](https://chromium.googlesource.com/angle/angle/)
- [ANGLE README](https://chromium.googlesource.com/angle/angle/+/main/README.md)
- [ANGLE Debugging Tips](https://chromium.googlesource.com/angle/angle/+/HEAD/doc/DebuggingTips.md)
