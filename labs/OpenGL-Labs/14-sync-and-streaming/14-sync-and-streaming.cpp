#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

// ============================================================
// 1. 平台层：窗口、EGLDisplay、Surface 和两个共享 Context
// ============================================================

struct App {
    HWND window = nullptr;
    bool running = true;
    int width = 900;
    int height = 700;

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface windowSurface = EGL_NO_SURFACE;
    EGLSurface uploadSurface = EGL_NO_SURFACE;

    EGLContext renderContext = EGL_NO_CONTEXT;
    EGLContext uploadContext = EGL_NO_CONTEXT;
};

static App* gApp = nullptr;

static void failEgl(const char* operation) {
    std::fprintf(
        stderr,
        "%s failed, EGL error: 0x%04X\n",
        operation,
        eglGetError()
    );

    std::exit(EXIT_FAILURE);
}

static void checkEgl(EGLBoolean result, const char* operation) {
    if (result != EGL_TRUE) {
        failEgl(operation);
    }
}

static LRESULT CALLBACK windowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    switch (message) {
    case WM_CLOSE:
        gApp->running = false;
        return 0;

    case WM_SIZE:
        gApp->width = LOWORD(lParam);
        gApp->height = HIWORD(lParam);
        return 0;

    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

static void createNativeWindow(App& app) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"SyncStreamingWindow";

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = className;

    if (!RegisterClassExW(&windowClass)) {
        std::fprintf(stderr, "RegisterClassExW failed\n");
        std::exit(EXIT_FAILURE);
    }

    app.window = CreateWindowExW(
        0,
        className,
        L"14 - Sync and Streaming",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        app.width,
        app.height,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (!app.window) {
        std::fprintf(stderr, "CreateWindowExW failed\n");
        std::exit(EXIT_FAILURE);
    }

    ShowWindow(app.window, SW_SHOWDEFAULT);
}

static void initializeEgl(App& app) {
    app.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (app.display == EGL_NO_DISPLAY) {
        failEgl("eglGetDisplay");
    }

    checkEgl(eglInitialize(app.display, nullptr, nullptr), "eglInitialize");
    checkEgl(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");

    // 同一个 EGLConfig 同时支持窗口 Surface 和离屏 Pbuffer。
    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config = nullptr;
    EGLint configCount = 0;

    checkEgl(
        eglChooseConfig(
            app.display,
            configAttributes,
            &config,
            1,
            &configCount
        ),
        "eglChooseConfig"
    );

    if (configCount == 0) {
        std::fprintf(stderr, "No matching EGLConfig found\n");
        std::exit(EXIT_FAILURE);
    }

    app.windowSurface = eglCreateWindowSurface(
        app.display,
        config,
        app.window,
        nullptr
    );

    if (app.windowSurface == EGL_NO_SURFACE) {
        failEgl("eglCreateWindowSurface");
    }

    const EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    // renderContext 只绑定到主线程。
    app.renderContext = eglCreateContext(
        app.display,
        config,
        EGL_NO_CONTEXT,
        contextAttributes
    );

    if (app.renderContext == EGL_NO_CONTEXT) {
        failEgl("eglCreateContext render");
    }

    // 上传线程不需要显示窗口，但 Context 仍然需要绘制目标。
    // 这里创建一个 1x1 的离屏 Pbuffer Surface。
    const EGLint pbufferAttributes[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };

    app.uploadSurface = eglCreatePbufferSurface(
        app.display,
        config,
        pbufferAttributes
    );

    if (app.uploadSurface == EGL_NO_SURFACE) {
        failEgl("eglCreatePbufferSurface");
    }

    // 第三个参数是 share_context。
    // uploadContext 可以访问 renderContext 的 Texture、Buffer 和 Sync。
    app.uploadContext = eglCreateContext(
        app.display,
        config,
        app.renderContext,
        contextAttributes
    );

    if (app.uploadContext == EGL_NO_CONTEXT) {
        failEgl("eglCreateContext upload");
    }

    // 主线程从此成为渲染线程。
    checkEgl(
        eglMakeCurrent(
            app.display,
            app.windowSurface,
            app.windowSurface,
            app.renderContext
        ),
        "eglMakeCurrent render"
    );

    checkEgl(eglSwapInterval(app.display, 1), "eglSwapInterval");

    std::printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
}

// ============================================================
// 2. 渲染资源：矩形 Mesh 和 Shader Program
// ============================================================

struct Renderer {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
};

static GLuint compileShader(
    GLenum type,
    const char* source,
    const char* name
) {
    GLuint shader = glCreateShader(type);

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok) {
        char log[2048]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);

        std::fprintf(stderr, "%s compile failed:\n%s\n", name, log);
        std::exit(EXIT_FAILURE);
    }

    return shader;
}

static GLuint createProgram() {
    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec2 vTexCoord;

uniform sampler2D uTexture;

out vec4 fragColor;

void main() {
    fragColor = texture(uTexture, vTexCoord);
}
)";

    GLuint vertexShader =
        compileShader(GL_VERTEX_SHADER, vertexSource, "vertex shader");

    GLuint fragmentShader =
        compileShader(GL_FRAGMENT_SHADER, fragmentSource, "fragment shader");

    GLuint program = glCreateProgram();

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);

    if (!ok) {
        char log[2048]{};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);

        std::fprintf(stderr, "program link failed:\n%s\n", log);
        std::exit(EXIT_FAILURE);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

static Renderer createRenderer() {
    const float vertices[] = {
        // position       // uv
        -0.85f, -0.85f,   0.0f, 0.0f,
         0.85f, -0.85f,   2.0f, 0.0f,
         0.85f,  0.85f,   2.0f, 2.0f,
        -0.85f,  0.85f,   0.0f, 2.0f
    };

    const std::uint16_t indices[] = {
        0, 1, 2,
        2, 3, 0
    };

    Renderer renderer;
    renderer.program = createProgram();

    glGenVertexArrays(1, &renderer.vao);
    glGenBuffers(1, &renderer.vbo);
    glGenBuffers(1, &renderer.ebo);

    glBindVertexArray(renderer.vao);

    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer.ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        sizeof(indices),
        indices,
        GL_STATIC_DRAW
    );

    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        nullptr
    );
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<void*>(2 * sizeof(float))
    );
    glEnableVertexAttribArray(1);

    glUseProgram(renderer.program);

    // sampler2D 保存的是纹理单元索引，而不是 Texture 对象 ID。
    glUniform1i(
        glGetUniformLocation(renderer.program, "uTexture"),
        0
    );

    return renderer;
}

// ============================================================
// 3. 双缓冲纹理流：两个线程之间共享的状态
// ============================================================

enum class SlotState {
    Free,       // 上传线程可以写入
    Uploading,  // 上传线程正在更新纹理
    Ready,      // 上传完成，等待渲染线程使用
    Displaying  // 渲染线程正在使用
};

struct TextureSlot {
    GLuint texture = 0;

    // 上传线程创建，渲染线程等待并删除。
    GLsync uploadFence = nullptr;

    // 渲染线程创建，上传线程等待并删除。
    GLsync reuseFence = nullptr;

    SlotState state = SlotState::Free;
};

struct TextureStream {
    std::array<TextureSlot, 2> slots;

    // mutex 和 condition 只同步 CPU 线程。
    std::mutex mutex;
    std::condition_variable condition;

    std::atomic<bool> running = true;
};

static void initializeStreamingTextures(TextureStream& stream) {
    constexpr int width = 256;
    constexpr int height = 256;

    for (TextureSlot& slot : stream.slots) {
        glGenTextures(1, &slot.texture);
        glBindTexture(GL_TEXTURE_2D, slot.texture);

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

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_S,
            GL_REPEAT
        );

        glTexParameteri(
            GL_TEXTURE_2D,
            GL_TEXTURE_WRAP_T,
            GL_REPEAT
        );

        // 只分配 GPU 存储空间，暂时不上传像素。
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            width,
            height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr
        );
    }

    // 初始化只执行一次。
    // 为了让上传 Context 稳定看到纹理存储，启动线程前等待完成。
    glFinish();
}

static void fillPixels(
    std::vector<std::uint8_t>& pixels,
    int width,
    int height,
    int frameIndex
) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            bool checker =
                ((x / 16) + (y / 16) + frameIndex / 4) % 2 == 0;

            std::size_t offset =
                static_cast<std::size_t>((y * width + x) * 4);

            pixels[offset + 0] = checker ? 245 : 30;

            pixels[offset + 1] = checker
                ? static_cast<std::uint8_t>((frameIndex * 4) % 255)
                : 80;

            pixels[offset + 2] = checker ? 45 : 230;
            pixels[offset + 3] = 255;
        }
    }
}

// CPU 等待 GPU Fence。
// 适用于“复用资源前，必须确认 GPU 已经使用完毕”的场景。
static void waitFenceOnCpu(GLsync fence) {
    if (!fence) {
        return;
    }

    while (true) {
        GLenum result = glClientWaitSync(
            fence,
            0,
            5'000'000 // 最多等待 5 ms，然后再次检查。
        );

        if (
            result == GL_ALREADY_SIGNALED ||
            result == GL_CONDITION_SATISFIED
            ) {
            return;
        }

        if (result == GL_WAIT_FAILED) {
            std::fprintf(stderr, "glClientWaitSync failed\n");
            std::exit(EXIT_FAILURE);
        }
    }
}

// ============================================================
// 4. 上传线程：持续生成像素并更新空闲纹理
// ============================================================

static void uploadLoop(
    App& app,
    TextureStream& stream
) {
    // 一个 Context 同时只能绑定到一个线程。
    checkEgl(
        eglMakeCurrent(
            app.display,
            app.uploadSurface,
            app.uploadSurface,
            app.uploadContext
        ),
        "eglMakeCurrent upload"
    );

    constexpr int width = 256;
    constexpr int height = 256;

    std::vector<std::uint8_t> pixels(width * height * 4);

    int frameIndex = 0;

    while (stream.running.load()) {
        int slotIndex = -1;
        GLsync reuseFence = nullptr;

        {
            std::unique_lock<std::mutex> lock(stream.mutex);

            // 等待一个可写入的纹理槽位。
            stream.condition.wait(lock, [&] {
                return !stream.running.load() ||
                    stream.slots[0].state == SlotState::Free ||
                    stream.slots[1].state == SlotState::Free;
                });

            if (!stream.running.load()) {
                break;
            }

            for (int index = 0; index < 2; ++index) {
                if (stream.slots[index].state == SlotState::Free) {
                    slotIndex = index;
                    break;
                }
            }

            TextureSlot& slot = stream.slots[slotIndex];

            slot.state = SlotState::Uploading;

            // 渲染线程可能留下一个 Fence。
            // 上传前必须确认 GPU 已经不再读取此纹理。
            reuseFence = slot.reuseFence;
            slot.reuseFence = nullptr;
        }

        waitFenceOnCpu(reuseFence);

        if (reuseFence) {
            glDeleteSync(reuseFence);
        }

        fillPixels(pixels, width, height, frameIndex++);

        TextureSlot& slot = stream.slots[slotIndex];

        glBindTexture(GL_TEXTURE_2D, slot.texture);

        // 更新已有纹理的像素，不重新分配存储空间。
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0,
            0,
            width,
            height,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixels.data()
        );

        // Fence 位于上传命令之后。
        // Fence 触发后，前面的 glTexSubImage2D 已经完成。
        GLsync uploadFence =
            glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

        // 将上传 Context 中积累的命令提交给驱动。
        // 没有 Flush，渲染线程可能一直等不到 Fence。
        glFlush();

        {
            std::lock_guard<std::mutex> lock(stream.mutex);

            slot.uploadFence = uploadFence;
            slot.state = SlotState::Ready;
        }

        stream.condition.notify_one();

        // 模拟摄像头、视频或网络数据流的更新节奏。
        std::this_thread::sleep_for(
            std::chrono::milliseconds(16)
        );
    }

    checkEgl(
        eglMakeCurrent(
            app.display,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT
        ),
        "eglMakeCurrent upload cleanup"
    );

    eglReleaseThread();
}

// ============================================================
// 5. 渲染线程：切换到最新准备完成的纹理
// ============================================================

static void activateReadyTexture(
    TextureStream& stream,
    int& activeSlot
) {
    int nextSlot = -1;
    GLsync uploadFence = nullptr;

    {
        std::lock_guard<std::mutex> lock(stream.mutex);

        for (int index = 0; index < 2; ++index) {
            if (stream.slots[index].state == SlotState::Ready) {
                nextSlot = index;
                break;
            }
        }

        // 没有新数据时，继续显示旧纹理。
        if (nextSlot == -1) {
            return;
        }

        TextureSlot& next = stream.slots[nextSlot];

        next.state = SlotState::Displaying;
        uploadFence = next.uploadFence;
        next.uploadFence = nullptr;
    }

    if (activeSlot != -1) {
        // 此 Fence 排在旧纹理最后一次 Draw Call 之后。
        // 上传线程等待它触发后，才能安全覆盖旧纹理。
        GLsync reuseFence =
            glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

        glFlush();

        {
            std::lock_guard<std::mutex> lock(stream.mutex);

            TextureSlot& previous = stream.slots[activeSlot];

            previous.reuseFence = reuseFence;
            previous.state = SlotState::Free;
        }

        stream.condition.notify_one();
    }

    // GPU 端等待上传完成。
    //
    // glWaitSync 不会阻塞 CPU，而是将等待关系插入当前 GPU 命令流。
    // 后续 Draw Call 在 uploadFence 触发后才会读取纹理。
    glWaitSync(
        uploadFence,
        0,
        GL_TIMEOUT_IGNORED
    );

    glDeleteSync(uploadFence);

    activeSlot = nextSlot;
}

static void processMessages(App& app) {
    MSG message{};

    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

// ============================================================
// 6. 清理资源
// ============================================================

static void destroyRenderer(Renderer& renderer) {
    glDeleteBuffers(1, &renderer.ebo);
    glDeleteBuffers(1, &renderer.vbo);
    glDeleteVertexArrays(1, &renderer.vao);
    glDeleteProgram(renderer.program);
}

static void destroyStreamingTextures(TextureStream& stream) {
    // 等待渲染 Context 已提交的绘制命令完成。
    glFinish();

    for (TextureSlot& slot : stream.slots) {
        // 上传线程可能已经提交了数据，但渲染线程还没有消费。
        waitFenceOnCpu(slot.uploadFence);

        if (slot.uploadFence) {
            glDeleteSync(slot.uploadFence);
        }

        if (slot.reuseFence) {
            glDeleteSync(slot.reuseFence);
        }

        glDeleteTextures(1, &slot.texture);
    }
}

static void destroyEgl(App& app) {
    checkEgl(
        eglMakeCurrent(
            app.display,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT
        ),
        "eglMakeCurrent render cleanup"
    );

    checkEgl(
        eglDestroyContext(app.display, app.uploadContext),
        "eglDestroyContext upload"
    );

    checkEgl(
        eglDestroySurface(app.display, app.uploadSurface),
        "eglDestroySurface upload"
    );

    checkEgl(
        eglDestroyContext(app.display, app.renderContext),
        "eglDestroyContext render"
    );

    checkEgl(
        eglDestroySurface(app.display, app.windowSurface),
        "eglDestroySurface window"
    );

    checkEgl(eglTerminate(app.display), "eglTerminate");

    eglReleaseThread();

    if (app.window) {
        DestroyWindow(app.window);
    }
}

// ============================================================
// 7. 主线程：渲染循环
// ============================================================

int main() {
    App app;
    gApp = &app;

    createNativeWindow(app);
    initializeEgl(app);

    Renderer renderer = createRenderer();

    TextureStream stream;
    initializeStreamingTextures(stream);

    // 上传线程开始持续更新 Texture。
    std::thread uploader(
        uploadLoop,
        std::ref(app),
        std::ref(stream)
    );

    int activeSlot = -1;

    while (app.running) {
        processMessages(app);

        if (
            !app.running ||
            app.width == 0 ||
            app.height == 0
            ) {
            Sleep(16);
            continue;
        }

        // 尝试切换到上传线程刚刚准备好的纹理。
        activateReadyTexture(stream, activeSlot);

        glViewport(0, 0, app.width, app.height);
        glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (activeSlot != -1) {
            glUseProgram(renderer.program);
            glBindVertexArray(renderer.vao);

            glActiveTexture(GL_TEXTURE0);

            // 另一个 Context 修改共享对象后，当前 Context 重新绑定它。
            glBindTexture(
                GL_TEXTURE_2D,
                stream.slots[activeSlot].texture
            );

            glDrawElements(
                GL_TRIANGLES,
                6,
                GL_UNSIGNED_SHORT,
                nullptr
            );
        }

        checkEgl(
            eglSwapBuffers(app.display, app.windowSurface),
            "eglSwapBuffers"
        );
    }

    // 先停止并等待上传线程退出。
    stream.running.store(false);
    stream.condition.notify_all();

    uploader.join();

    // 此时只有主线程继续使用 GLES 资源。
    destroyStreamingTextures(stream);
    destroyRenderer(renderer);
    destroyEgl(app);

    return EXIT_SUCCESS;
}