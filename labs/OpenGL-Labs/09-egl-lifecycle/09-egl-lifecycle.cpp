#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdio>
#include <cstdlib>

struct App {
    HWND window = nullptr;
    bool running = true;
    int width = 800;
    int height = 600;

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;

    GLuint program = 0;
    GLuint vertexArray = 0;
    GLuint vertexBuffer = 0;
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
        // 先退出渲染循环，再按顺序释放 GLES、EGL 和 Win32 资源。
        gApp->running = false;
        return 0;

    case WM_SIZE:
        gApp->width = LOWORD(lParam);
        gApp->height = HIWORD(lParam);
        return 0;

    case WM_DESTROY:
        gApp->running = false;
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

static void createNativeWindow(App& app, HINSTANCE instance) {
    const wchar_t* className = L"EglLifecycleWindow";

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
        L"09 - EGL Lifecycle",
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
    // EGLDisplay 表示应用与 EGL 实现之间的连接。
    // 当前链接的是 ANGLE 提供的 libEGL，因此这里会进入 ANGLE。
    app.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (app.display == EGL_NO_DISPLAY) {
        failEgl("eglGetDisplay");
    }

    EGLint major = 0;
    EGLint minor = 0;
    checkEgl(eglInitialize(app.display, &major, &minor), "eglInitialize");
    std::printf("EGL_VERSION : %d.%d\n", major, minor);

    // 告诉 EGL：后续创建的是 OpenGL ES Context。
    checkEgl(eglBindAPI(EGL_OPENGL_ES_API), "eglBindAPI");

    // EGLConfig 描述颜色、深度缓冲区以及支持的 Surface 和 API 类型。
    // 它不是 GLES 状态，而是 EGL 创建渲染环境时使用的配置模板。
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

    // EGLSurface 将 Win32 窗口变成 GLES 可以绘制的目标。
    app.surface = eglCreateWindowSurface(
        app.display,
        config,
        app.window,
        nullptr
    );
    if (app.surface == EGL_NO_SURFACE) {
        failEgl("eglCreateWindowSurface");
    }

    const EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    // EGLContext 保存 GLES 状态，例如当前 Program、Buffer 绑定和渲染开关。
    // 第四个参数用于指定共享 Context。本实验暂时不共享资源。
    app.context = eglCreateContext(
        app.display,
        config,
        EGL_NO_CONTEXT,
        contextAttributes
    );
    if (app.context == EGL_NO_CONTEXT) {
        failEgl("eglCreateContext");
    }

    // GLES API 操作的是“当前线程上绑定的 Context”。
    // 在调用 glCreateShader、glBindBuffer 等 GLES 函数前，必须先 MakeCurrent。
    checkEgl(
        eglMakeCurrent(
            app.display,
            app.surface,
            app.surface,
            app.context
        ),
        "eglMakeCurrent"
    );

    // 1 通常表示开启垂直同步：每次交换缓冲区至少等待一个显示刷新周期。
    checkEgl(eglSwapInterval(app.display, 1), "eglSwapInterval");

    std::printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
    std::printf("GL_VENDOR   : %s\n", glGetString(GL_VENDOR));
}

static GLuint compileShader(GLenum type, const char* source, const char* name) {
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

static void initializeGles(App& app) {
    // 顶点数据存放在 CPU 内存中。每个顶点由位置 vec2 和颜色 vec3 组成。
    const float vertices[] = {
        // x,     y,      r,    g,    b
        -0.7f,  -0.6f,   1.0f, 0.2f, 0.2f,
         0.7f,  -0.6f,   0.2f, 1.0f, 0.2f,
         0.0f,   0.7f,   0.2f, 0.4f, 1.0f
    };

    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec3 aColor;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec3 vColor;
out vec4 fragColor;

void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

    GLuint vertexShader =
        compileShader(GL_VERTEX_SHADER, vertexSource, "vertex shader");
    GLuint fragmentShader =
        compileShader(GL_FRAGMENT_SHADER, fragmentSource, "fragment shader");

    app.program = glCreateProgram();
    glAttachShader(app.program, vertexShader);
    glAttachShader(app.program, fragmentShader);
    glLinkProgram(app.program);

    GLint ok = GL_FALSE;
    glGetProgramiv(app.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048]{};
        glGetProgramInfoLog(app.program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "program link failed:\n%s\n", log);
        std::exit(EXIT_FAILURE);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // VBO 保存上传到 GPU 可访问内存中的原始字节。
    glGenBuffers(1, &app.vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, app.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // VAO 记录“如何解释 VBO 中的字节”。
    glGenVertexArrays(1, &app.vertexArray);
    glBindVertexArray(app.vertexArray);

    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        reinterpret_cast<void*>(0)
    );
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        reinterpret_cast<void*>(2 * sizeof(float))
    );
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void renderFrame(const App& app) {
    glViewport(0, 0, app.width, app.height);
    glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Draw Call 会消费当前 Context 中已经设置好的 GLES 状态。
    glUseProgram(app.program);
    glBindVertexArray(app.vertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void processWindowMessages(App& app) {
    MSG message{};

    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            app.running = false;
            return;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

static void cleanup(App& app) {
    // GLES 对象必须在 Context 仍然绑定到当前线程时删除。
    glDeleteBuffers(1, &app.vertexBuffer);
    glDeleteVertexArrays(1, &app.vertexArray);
    glDeleteProgram(app.program);

    // 解除当前线程与 Context、Surface 的绑定。
    checkEgl(
        eglMakeCurrent(
            app.display,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT
        ),
        "eglMakeCurrent cleanup"
    );

    checkEgl(eglDestroyContext(app.display, app.context), "eglDestroyContext");
    checkEgl(eglDestroySurface(app.display, app.surface), "eglDestroySurface");
    checkEgl(eglTerminate(app.display), "eglTerminate");
    eglReleaseThread();

    if (app.window) {
        DestroyWindow(app.window);
    }
}

int main() {
    App app;
    gApp = &app;

    createNativeWindow(app, GetModuleHandleW(nullptr));
    initializeEgl(app);
    initializeGles(app);

    while (app.running) {
        processWindowMessages(app);

        // 窗口最小化后宽高可能为 0，此时暂停绘制。
        if (!app.running || app.width == 0 || app.height == 0) {
            Sleep(16);
            continue;
        }

        renderFrame(app);

        // 默认 Framebuffer 通常采用双缓冲。
        // SwapBuffers 将已经绘制完成的后缓冲区提交到窗口系统显示。
        if (eglSwapBuffers(app.display, app.surface) != EGL_TRUE) {
            failEgl("eglSwapBuffers");
        }
    }

    cleanup(app);
    return EXIT_SUCCESS;
}
