#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// ============================================================
// 1. CPU 资源描述与 GPU 资源句柄分离
// ============================================================
//
// CPU 资源描述：Context 丢失后仍然存在。
// GPU 资源句柄：Context 销毁后全部失效，必须重建。

struct CpuResources {
    int textureWidth = 128;
    int textureHeight = 128;
    std::vector<std::uint8_t> texturePixels;
};

struct GpuResources {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLuint texture = 0;

    GLint timeLocation = -1;
    GLint textureLocation = -1;

    GLsizei indexCount = 0;
    int generation = 0;
};

struct EglState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
};

struct App {
    HWND window = nullptr;
    bool running = true;
    bool recreateRequested = false;
    int width = 900;
    int height = 700;
};

static App* gApp = nullptr;

// ============================================================
// 2. Win32 窗口
// ============================================================

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

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            gApp->running = false;
            return 0;
        }

        if (wParam == 'R') {
            gApp->recreateRequested = true;
            return 0;
        }

        return 0;

    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

static void createWindow(App& app) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"ContextRecoveryWindow";

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
        L"29 - Context Recovery",
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

static void processMessages(App& app) {
    MSG message{};

    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

// ============================================================
// 3. EGL 创建与销毁
// ============================================================

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

static void initializeEglDisplay(EglState& egl) {
    egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (egl.display == EGL_NO_DISPLAY) {
        failEgl("eglGetDisplay");
    }

    EGLint major = 0;
    EGLint minor = 0;

    checkEgl(
        eglInitialize(egl.display, &major, &minor),
        "eglInitialize"
    );

    checkEgl(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");

    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,

        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,

        EGL_DEPTH_SIZE, 24,

        EGL_NONE
    };

    EGLint configCount = 0;

    checkEgl(
        eglChooseConfig(
            egl.display,
            configAttributes,
            &egl.config,
            1,
            &configCount
        ),
        "eglChooseConfig"
    );

    if (configCount == 0) {
        std::fprintf(stderr, "No matching EGLConfig found\n");
        std::exit(EXIT_FAILURE);
    }

    std::printf("EGL_VERSION : %d.%d\n", major, minor);
}

static void createEglSurfaceAndContext(
    EglState& egl,
    HWND window
) {
    egl.surface = eglCreateWindowSurface(
        egl.display,
        egl.config,
        window,
        nullptr
    );

    if (egl.surface == EGL_NO_SURFACE) {
        failEgl("eglCreateWindowSurface");
    }

    const EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    egl.context = eglCreateContext(
        egl.display,
        egl.config,
        EGL_NO_CONTEXT,
        contextAttributes
    );

    if (egl.context == EGL_NO_CONTEXT) {
        failEgl("eglCreateContext");
    }

    checkEgl(
        eglMakeCurrent(
            egl.display,
            egl.surface,
            egl.surface,
            egl.context
        ),
        "eglMakeCurrent"
    );

    checkEgl(eglSwapInterval(egl.display, 1), "eglSwapInterval");

    std::printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
}

static void destroyEglSurfaceAndContext(EglState& egl) {
    if (egl.display == EGL_NO_DISPLAY) {
        return;
    }

    eglMakeCurrent(
        egl.display,
        EGL_NO_SURFACE,
        EGL_NO_SURFACE,
        EGL_NO_CONTEXT
    );

    if (egl.context != EGL_NO_CONTEXT) {
        eglDestroyContext(egl.display, egl.context);
        egl.context = EGL_NO_CONTEXT;
    }

    if (egl.surface != EGL_NO_SURFACE) {
        eglDestroySurface(egl.display, egl.surface);
        egl.surface = EGL_NO_SURFACE;
    }

    eglReleaseThread();
}

static void terminateEgl(EglState& egl) {
    destroyEglSurfaceAndContext(egl);

    if (egl.display != EGL_NO_DISPLAY) {
        eglTerminate(egl.display);
        egl.display = EGL_NO_DISPLAY;
    }
}

// ============================================================
// 4. CPU 资源
// ============================================================

static CpuResources createCpuResources() {
    CpuResources resources;

    resources.texturePixels.resize(
        resources.textureWidth *
        resources.textureHeight *
        4
    );

    for (int y = 0; y < resources.textureHeight; ++y) {
        for (int x = 0; x < resources.textureWidth; ++x) {
            bool checker =
                ((x / 16) + (y / 16)) % 2 == 0;

            int offset =
                (y * resources.textureWidth + x) * 4;

            resources.texturePixels[offset + 0] =
                checker ? 245 : 40;

            resources.texturePixels[offset + 1] =
                checker ? 180 : 80;

            resources.texturePixels[offset + 2] =
                static_cast<std::uint8_t>(
                    x * 255 / resources.textureWidth
                    );

            resources.texturePixels[offset + 3] = 255;
        }
    }

    return resources;
}

// ============================================================
// 5. Shader 与 GPU 资源
// ============================================================

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

uniform mediump float uTime;

out vec2 vTexCoord;

void main() {
    float angle = uTime * 0.8;
    float c = cos(angle);
    float s = sin(angle);

    mat2 rotation = mat2(
        c, s,
       -s, c
    );

    vec2 position = rotation * aPosition * 0.72;

    gl_Position = vec4(position, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform mediump float uTime;

out vec4 fragColor;

void main() {
    vec4 texel = texture(uTexture, vTexCoord);

    float pulse =
        0.75 + 0.25 * sin(uTime * 2.0);

    fragColor = vec4(texel.rgb * pulse, 1.0);
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

static GpuResources createGpuResources(
    const CpuResources& cpu,
    int generation
) {
    const float vertices[] = {
        // position       // uv
        -1.0f, -1.0f,     0.0f, 0.0f,
         1.0f, -1.0f,     1.0f, 0.0f,
         1.0f,  1.0f,     1.0f, 1.0f,
        -1.0f,  1.0f,     0.0f, 1.0f
    };

    const std::uint16_t indices[] = {
        0, 1, 2,
        2, 3, 0
    };

    GpuResources gpu;
    gpu.generation = generation;
    gpu.indexCount = 6;

    gpu.program = createProgram();

    gpu.timeLocation =
        glGetUniformLocation(gpu.program, "uTime");

    gpu.textureLocation =
        glGetUniformLocation(gpu.program, "uTexture");

    glGenVertexArrays(1, &gpu.vao);
    glGenBuffers(1, &gpu.vbo);
    glGenBuffers(1, &gpu.ebo);

    glBindVertexArray(gpu.vao);

    glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ebo);
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

    glBindVertexArray(0);

    glGenTextures(1, &gpu.texture);
    glBindTexture(GL_TEXTURE_2D, gpu.texture);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        cpu.textureWidth,
        cpu.textureHeight,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        cpu.texturePixels.data()
    );

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

    glUseProgram(gpu.program);
    glUniform1i(gpu.textureLocation, 0);

    std::printf(
        "GPU resources created, generation = %d\n",
        generation
    );

    return gpu;
}

static void destroyGpuResources(GpuResources& gpu) {
    if (gpu.texture) {
        glDeleteTextures(1, &gpu.texture);
        gpu.texture = 0;
    }

    if (gpu.ebo) {
        glDeleteBuffers(1, &gpu.ebo);
        gpu.ebo = 0;
    }

    if (gpu.vbo) {
        glDeleteBuffers(1, &gpu.vbo);
        gpu.vbo = 0;
    }

    if (gpu.vao) {
        glDeleteVertexArrays(1, &gpu.vao);
        gpu.vao = 0;
    }

    if (gpu.program) {
        glDeleteProgram(gpu.program);
        gpu.program = 0;
    }
}

// ============================================================
// 6. 渲染与重建
// ============================================================

static void renderFrame(
    const App& app,
    const GpuResources& gpu,
    float elapsedSeconds
) {
    glViewport(0, 0, app.width, app.height);

    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(gpu.program);

    glUniform1f(
        gpu.timeLocation,
        elapsedSeconds
    );

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gpu.texture);

    glBindVertexArray(gpu.vao);

    glDrawElements(
        GL_TRIANGLES,
        gpu.indexCount,
        GL_UNSIGNED_SHORT,
        nullptr
    );
}

static void recreateContextAndResources(
    App& app,
    EglState& egl,
    const CpuResources& cpu,
    GpuResources& gpu,
    int& generation
) {
    std::printf("\n--- Simulated context recovery ---\n");

    // 模拟场景：当前 Context 仍然可用，所以先正常删除 GPU 对象。
    // 真正的 EGL_CONTEXT_LOST 发生时，GL 对象可能已经不可用了，
    // 实际项目应直接丢弃旧句柄，并从 CPU 资源描述重建。
    destroyGpuResources(gpu);

    destroyEglSurfaceAndContext(egl);

    createEglSurfaceAndContext(egl, app.window);

    ++generation;

    gpu = createGpuResources(cpu, generation);

    app.recreateRequested = false;

    std::printf("--- Recovery finished ---\n\n");
}

// ============================================================
// 7. 主函数
// ============================================================

int main() {
    App app;
    gApp = &app;

    createWindow(app);

    EglState egl;
    initializeEglDisplay(egl);
    createEglSurfaceAndContext(egl, app.window);

    CpuResources cpu = createCpuResources();

    int generation = 1;
    GpuResources gpu =
        createGpuResources(cpu, generation);

    std::printf("\nPress R to simulate context loss and rebuild.\n");
    std::printf("Press ESC to exit.\n\n");

    const double startSeconds =
        static_cast<double>(GetTickCount64()) / 1000.0;

    while (app.running) {
        processMessages(app);

        if (!app.running) {
            break;
        }

        if (app.recreateRequested) {
            recreateContextAndResources(
                app,
                egl,
                cpu,
                gpu,
                generation
            );
        }

        if (app.width == 0 || app.height == 0) {
            Sleep(16);
            continue;
        }

        const double nowSeconds =
            static_cast<double>(GetTickCount64()) / 1000.0;

        float elapsedSeconds =
            static_cast<float>(nowSeconds - startSeconds);

        renderFrame(app, gpu, elapsedSeconds);

        EGLBoolean swapped =
            eglSwapBuffers(egl.display, egl.surface);

        if (swapped != EGL_TRUE) {
            EGLint error = eglGetError();

            if (error == EGL_CONTEXT_LOST) {
                std::printf("EGL_CONTEXT_LOST detected\n");

                app.recreateRequested = true;
                continue;
            }

            std::fprintf(
                stderr,
                "eglSwapBuffers failed, EGL error: 0x%04X\n",
                error
            );

            break;
        }
    }

    destroyGpuResources(gpu);
    terminateEgl(egl);

    if (app.window) {
        DestroyWindow(app.window);
    }

    return EXIT_SUCCESS;
}