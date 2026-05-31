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

# 19. 学习完成检查清单

## EGL

- [ ] 能够独立写出 EGL 初始化流程
- [ ] 能够解释 Display、Config、Surface 和 Context
- [ ] 理解 `eglMakeCurrent()` 与线程的关系
- [ ] 能够按正确顺序销毁 EGL 对象

## GLES 状态机

- [ ] 能够解释 `glBind*()` 为什么影响后续调用
- [ ] 能够解释 Draw Call 消费哪些状态
- [ ] 能够区分 VBO、EBO 与 VAO
- [ ] 能够区分 Texture Object 与 Texture Unit
- [ ] 能够使用 FBO 实现离屏渲染

## Shader

- [ ] 能够阅读 Vertex Shader 与 Fragment Shader
- [ ] 能够区分 Attribute、Uniform 和插值变量
- [ ] 能够解释 MVP 矩阵
- [ ] 能够解释纹理采样数据流
- [ ] 能够解释基础漫反射和法线用途

## 渲染循环

- [ ] 能够拆分初始化、更新、渲染和清理阶段
- [ ] 能够区分 Frame、Object 与 Material 数据
- [ ] 能够从 Draw Call 反向追踪状态来源

## 多线程

- [ ] 理解 Context 不能同时绑定到多个线程
- [ ] 能够创建共享 Context
- [ ] 能够列出可共享与不可共享对象
- [ ] 能够区分 CPU 同步与 GPU 同步
- [ ] 能够解释 Fence、Flush、Wait 和 Finish

---

# 20. 推荐实验顺序

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

# 21. 官方参考资料

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