#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

struct SharedTexture {
    GLuint texture = 0;
    GLsync fence = nullptr;
    bool ready = false;

    std::mutex mutex;
    std::condition_variable condition;
};

struct App {
    HWND window = nullptr;
    bool running = true;
    int width = 900;
    int height = 700;

    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface windowSurface = EGL_NO_SURFACE;
    EGLContext renderContext = EGL_NO_CONTEXT;

    EGLSurface uploadSurface = EGL_NO_SURFACE;
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
    const wchar_t* className = L"SharedContextWindow";

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
        L"13 - EGL Shared Context",
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

    const EGLint configAttributes[] = {
        // 同一个 Config 同时支持窗口 Surface 和离屏 Pbuffer。
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

    // 主 Context：只绑定到渲染线程。
    app.renderContext = eglCreateContext(
        app.display,
        config,
        EGL_NO_CONTEXT,
        contextAttributes
    );

    if (app.renderContext == EGL_NO_CONTEXT) {
        failEgl("eglCreateContext render");
    }

    // 上传线程不需要窗口，只需要一个很小的离屏 Surface。
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
    // uploadContext 与 renderContext 可以共享 Texture、Buffer 和 Sync。
    app.uploadContext = eglCreateContext(
        app.display,
        config,
        app.renderContext,
        contextAttributes
    );

    if (app.uploadContext == EGL_NO_CONTEXT) {
        failEgl("eglCreateContext upload");
    }

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

// 上传线程入口。
static void uploadTexture(App& app, SharedTexture& shared) {
    // 一个 Context 同一时间只能绑定到一个线程。
    checkEgl(
        eglMakeCurrent(
            app.display,
            app.uploadSurface,
            app.uploadSurface,
            app.uploadContext
        ),
        "eglMakeCurrent upload"
    );

    const std::uint8_t pixels[] = {
        // 红、黄、蓝、白棋盘格
        255,  40,  40, 255,   255, 220,  30, 255,
        255, 220,  30, 255,    40, 100, 255, 255
    };

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        2,
        2,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels
    );

    // Fence 位于纹理上传命令之后。
    // 当 Fence 被触发时，之前的 GPU 命令已经完成。
    GLsync fence =
        glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

    // 将上传线程中的命令提交给驱动。
    // 如果没有 Flush，另一个线程可能一直等不到 Fence。
    glFlush();

    {
        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.texture = texture;
        shared.fence = fence;
        shared.ready = true;
    }

    shared.condition.notify_one();

    // 上传线程不再使用 Context，解除绑定。
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

static void processMessages(App& app) {
    MSG message{};

    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

int main() {
    App app;
    gApp = &app;

    createNativeWindow(app);
    initializeEgl(app);

    // 渲染线程创建自己的 VAO、VBO 和 Program。
    // VAO 不属于共享对象，因此不要尝试在上传线程创建 VAO。
    const float vertices[] = {
        // position       // uv
        -0.8f, -0.8f,     0.0f, 0.0f,
         0.8f, -0.8f,     2.0f, 0.0f,
         0.8f,  0.8f,     2.0f, 2.0f,
        -0.8f,  0.8f,     0.0f, 2.0f
    };

    const std::uint16_t indices[] = {
        0, 1, 2,
        2, 3, 0
    };

    GLuint program = createProgram();

    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
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

    SharedTexture shared;

    // 后台线程使用共享 Context 上传纹理。
    std::thread worker(uploadTexture, std::ref(app), std::ref(shared));

    GLuint texture = 0;
    GLsync uploadFence = nullptr;

    {
        std::unique_lock<std::mutex> lock(shared.mutex);

        shared.condition.wait(lock, [&shared] {
            return shared.ready;
            });

        texture = shared.texture;
        uploadFence = shared.fence;
    }

    // GPU 端等待上传线程中的纹理命令完成。
    // 这不会让 CPU 忙等，而是将等待关系加入渲染命令流。
    glWaitSync(uploadFence, 0, GL_TIMEOUT_IGNORED);
    glDeleteSync(uploadFence);

    glUseProgram(program);
    glUniform1i(
        glGetUniformLocation(program, "uTexture"),
        0
    );

    while (app.running) {
        processMessages(app);

        if (!app.running || app.width == 0 || app.height == 0) {
            Sleep(16);
            continue;
        }

        glViewport(0, 0, app.width, app.height);
        glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glBindVertexArray(vao);

        glActiveTexture(GL_TEXTURE0);

        // 共享 Texture 仍然需要绑定到当前 Context 的纹理单元。
        glBindTexture(GL_TEXTURE_2D, texture);

        glDrawElements(
            GL_TRIANGLES,
            6,
            GL_UNSIGNED_SHORT,
            nullptr
        );

        checkEgl(
            eglSwapBuffers(app.display, app.windowSurface),
            "eglSwapBuffers"
        );
    }

    worker.join();

    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);

    checkEgl(
        eglMakeCurrent(
            app.display,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT
        ),
        "eglMakeCurrent render cleanup"
    );

    eglDestroyContext(app.display, app.uploadContext);
    eglDestroySurface(app.display, app.uploadSurface);

    eglDestroyContext(app.display, app.renderContext);
    eglDestroySurface(app.display, app.windowSurface);

    eglTerminate(app.display);
    eglReleaseThread();

    DestroyWindow(app.window);

    return EXIT_SUCCESS;
}