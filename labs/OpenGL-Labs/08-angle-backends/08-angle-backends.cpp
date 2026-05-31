#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct BackendOptions {
    const char* name;
    EGLint rendererType;
    EGLint deviceType;
};

// 根据命令行参数选择 ANGLE 后端。
static BackendOptions parseBackend(int argc, char* argv[]) {
    // 默认使用硬件 D3D11。
    if (argc < 2 || std::strcmp(argv[1], "--d3d11") == 0) {
        return {
            "D3D11 hardware",
            EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE
        };
    }

    // WARP 是 Windows 自带的 D3D11 软件光栅器。
    if (std::strcmp(argv[1], "--d3d11-warp") == 0) {
        return {
            "D3D11 WARP software renderer",
            EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_D3D_WARP_ANGLE
        };
    }

    // Vulkan 硬件后端。
    if (std::strcmp(argv[1], "--vulkan") == 0) {
        return {
            "Vulkan hardware",
            EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE
        };
    }

    // SwiftShader 是软件 Vulkan 实现。
    if (std::strcmp(argv[1], "--vulkan-swiftshader") == 0) {
        return {
            "Vulkan SwiftShader software renderer",
            EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_SWIFTSHADER_ANGLE
        };
    }

    // ANGLE 也可以把 GLES 翻译到桌面 OpenGL。
    if (std::strcmp(argv[1], "--opengl") == 0) {
        return {
            "Desktop OpenGL",
            EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE,
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE
        };
    }

    std::fprintf(stderr, "Unknown backend: %s\n", argv[1]);
    std::fprintf(stderr, "Available options:\n");
    std::fprintf(stderr, "  --d3d11\n");
    std::fprintf(stderr, "  --d3d11-warp\n");
    std::fprintf(stderr, "  --vulkan\n");
    std::fprintf(stderr, "  --vulkan-swiftshader\n");
    std::fprintf(stderr, "  --opengl\n");

    std::exit(EXIT_FAILURE);
}

// 输出最近一次 EGL 错误。
static void failEgl(const char* operation) {
    EGLint error = eglGetError();

    std::fprintf(
        stderr,
        "%s failed, EGL error: 0x%04X\n",
        operation,
        error
    );

    std::exit(EXIT_FAILURE);
}

// 检查 EGL API 是否执行成功。
static void checkEgl(EGLBoolean result, const char* operation) {
    if (result != EGL_TRUE) {
        failEgl(operation);
    }
}

static void checkShader(GLuint shader, const char* name) {
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "%s compile failed:\n%s\n", name, log);
        std::exit(EXIT_FAILURE);
    }
}

static GLuint compileShader(
    GLenum type,
    const char* source,
    const char* name
) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    checkShader(shader, name);
    return shader;
}

static GLuint createProgram(
    const char* vertexSource,
    const char* fragmentSource
) {
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
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "program link failed:\n%s\n", log);
        std::exit(EXIT_FAILURE);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

// Win32 窗口消息处理函数。
static LRESULT CALLBACK windowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    switch (message) {
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

int main(int argc, char* argv[]) {
    BackendOptions backend = parseBackend(argc, argv);
    std::printf("Requested backend: %s\n", backend.name);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    int showCommand = SW_SHOWDEFAULT;
    // 1. 创建 Win32 原生窗口。
    const wchar_t* windowClassName = L"DirectEglTriangleWindow";

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = windowClassName;

    if (!RegisterClassExW(&windowClass)) {
        std::fprintf(stderr, "RegisterClassExW failed\n");
        return EXIT_FAILURE;
    }

    HWND window = CreateWindowExW(
        0,
        windowClassName,
        L"07 - Direct EGL Triangle",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        800,
        600,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (!window) {
        std::fprintf(stderr, "CreateWindowExW failed\n");
        return EXIT_FAILURE;
    }

    ShowWindow(window, showCommand);

    // 2. 加载 ANGLE 提供的 eglGetPlatformDisplayEXT。
    //
    // 相比 eglGetDisplay()，这个扩展函数允许显式指定 ANGLE 后端。
    auto getPlatformDisplay =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT")
            );

    if (!getPlatformDisplay) {
        std::fprintf(stderr, "eglGetPlatformDisplayEXT is unavailable\n");
        return EXIT_FAILURE;
    }

    // 3. 通过 EGL_ANGLE_platform_angle 扩展指定 renderer 和 device。
    std::vector<EGLint> displayAttributes = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE,
        backend.rendererType,

        EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
        backend.deviceType,

        EGL_NONE
    };

    EGLDisplay display = getPlatformDisplay(
        EGL_PLATFORM_ANGLE_ANGLE,
        EGL_DEFAULT_DISPLAY,
        displayAttributes.data()
    );

    if (display == EGL_NO_DISPLAY) {
        failEgl("eglGetPlatformDisplayEXT");
    }

    EGLint eglMajor = 0;
    EGLint eglMinor = 0;

    checkEgl(
        eglInitialize(display, &eglMajor, &eglMinor),
        "eglInitialize"
    );

    checkEgl(
        eglBindAPI(EGL_OPENGL_ES_API),
        "eglBindAPI"
    );

    // 4. 选择适合窗口渲染和 OpenGL ES 3 的配置。
    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
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
            display,
            configAttributes,
            &config,
            1,
            &configCount
        ),
        "eglChooseConfig"
    );

    if (configCount == 0) {
        std::fprintf(stderr, "No matching EGLConfig found\n");
        return EXIT_FAILURE;
    }

    // 5. 将 Win32 HWND 包装成 EGLSurface。
    EGLSurface surface = eglCreateWindowSurface(
        display,
        config,
        window,
        nullptr
    );

    if (surface == EGL_NO_SURFACE) {
        failEgl("eglCreateWindowSurface");
    }

    // 6. 创建 OpenGL ES 3.0 context。
    const EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    EGLContext context = eglCreateContext(
        display,
        config,
        EGL_NO_CONTEXT,
        contextAttributes
    );

    if (context == EGL_NO_CONTEXT) {
        failEgl("eglCreateContext");
    }

    // 7. 将 context 绑定到当前线程和窗口 surface。
    checkEgl(
        eglMakeCurrent(display, surface, surface, context),
        "eglMakeCurrent"
    );

    // 开启垂直同步。
    checkEgl(
        eglSwapInterval(display, 1),
        "eglSwapInterval"
    );

    std::printf("EGL_VERSION : %d.%d\n", eglMajor, eglMinor);
    std::printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
    std::printf("GL_VENDOR   : %s\n", glGetString(GL_VENDOR));

    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
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

    GLuint program = createProgram(vertexSource, fragmentSource);

    float vertices[] = {
        // x      y      r     g     b
         0.0f,  0.6f,  1.0f, 0.2f, 0.2f,
        -0.6f, -0.5f,  0.2f, 1.0f, 0.2f,
         0.6f, -0.5f,  0.2f, 0.4f, 1.0f,
    };

    GLuint vao = 0;
    GLuint vbo = 0;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

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

    // 8. Win32 消息循环和 GLES 渲染循环。
    bool running = true;

    while (running) {
        MSG message{};

        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (!running) {
            break;
        }

        RECT clientRect{};
        GetClientRect(window, &clientRect);

        int width = clientRect.right - clientRect.left;
        int height = clientRect.bottom - clientRect.top;

        if (width == 0 || height == 0) {
            continue;
        }

        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // GLFW 示例使用 glfwSwapBuffers()。
        // 这里直接调用 EGL 完成前后缓冲交换。
        checkEgl(
            eglSwapBuffers(display, surface),
            "eglSwapBuffers"
        );
    }

    // 9. 释放 GLES、EGL 和 Win32 资源。
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);

    eglMakeCurrent(
        display,
        EGL_NO_SURFACE,
        EGL_NO_SURFACE,
        EGL_NO_CONTEXT
    );

    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);

    if (IsWindow(window)) {
        DestroyWindow(window);
    }

    UnregisterClassW(windowClassName, instance);

    return EXIT_SUCCESS;
}