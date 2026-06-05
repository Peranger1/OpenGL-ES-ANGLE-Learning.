#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

constexpr int PBUFFER_WIDTH = 512;
constexpr int PBUFFER_HEIGHT = 512;

struct EglObjects {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
};

struct Renderer {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
};

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

static const char* safeGlString(const GLubyte* value) {
    return value
        ? reinterpret_cast<const char*>(value)
        : "(null)";
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

out vec2 vUv;

void main() {
    // aPosition 位于 -1..1。
    // 转成 0..1 后传给 Fragment Shader，用于生成渐变颜色。
    vUv = aPosition * 0.5 + 0.5;

    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec2 vUv;

out vec4 fragColor;

void main() {
    vec2 center = vec2(0.5, 0.5);
    float distanceToCenter = length(vUv - center);

    vec3 background = vec3(vUv.x, vUv.y, 0.85);
    vec3 circle = vec3(1.0, 0.35, 0.15);

    float mask = smoothstep(0.30, 0.28, distanceToCenter);

    vec3 color = mix(background, circle, mask);

    fragColor = vec4(color, 1.0);
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

static EglObjects createPbufferContext() {
    EglObjects egl;

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

    std::printf("EGL_VERSION : %d.%d\n", major, minor);

    checkEgl(
        eglBindAPI(EGL_OPENGL_ES_API),
        "eglBindAPI"
    );

    // 这里关键是 EGL_SURFACE_TYPE 必须包含 EGL_PBUFFER_BIT。
    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
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
            egl.display,
            configAttributes,
            &config,
            1,
            &configCount
        ),
        "eglChooseConfig"
    );

    if (configCount == 0) {
        std::fprintf(stderr, "No matching pbuffer EGLConfig found\n");
        std::exit(EXIT_FAILURE);
    }

    const EGLint surfaceAttributes[] = {
        EGL_WIDTH, PBUFFER_WIDTH,
        EGL_HEIGHT, PBUFFER_HEIGHT,
        EGL_NONE
    };

    // Pbuffer Surface 是离屏 Surface。
    // 它没有对应的原生窗口，但仍然可以作为 GLES 的 draw/read surface。
    egl.surface = eglCreatePbufferSurface(
        egl.display,
        config,
        surfaceAttributes
    );

    if (egl.surface == EGL_NO_SURFACE) {
        failEgl("eglCreatePbufferSurface");
    }

    const EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    egl.context = eglCreateContext(
        egl.display,
        config,
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

    EGLint surfaceWidth = 0;
    EGLint surfaceHeight = 0;

    eglQuerySurface(
        egl.display,
        egl.surface,
        EGL_WIDTH,
        &surfaceWidth
    );

    eglQuerySurface(
        egl.display,
        egl.surface,
        EGL_HEIGHT,
        &surfaceHeight
    );

    std::printf(
        "PBUFFER     : %d x %d\n",
        surfaceWidth,
        surfaceHeight
    );

    std::printf(
        "GL_VERSION  : %s\n",
        safeGlString(glGetString(GL_VERSION))
    );

    std::printf(
        "GL_RENDERER : %s\n",
        safeGlString(glGetString(GL_RENDERER))
    );

    return egl;
}

static Renderer createRenderer() {
    // 全屏 Quad，覆盖整个 Pbuffer。
    const float vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,

         1.0f,  1.0f,
        -1.0f,  1.0f,
        -1.0f, -1.0f
    };

    Renderer renderer;
    renderer.program = createProgram();

    glGenVertexArrays(1, &renderer.vao);
    glGenBuffers(1, &renderer.vbo);

    glBindVertexArray(renderer.vao);

    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);

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
        2 * sizeof(float),
        nullptr
    );

    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return renderer;
}

static void renderToPbuffer(const Renderer& renderer) {
    glViewport(
        0,
        0,
        PBUFFER_WIDTH,
        PBUFFER_HEIGHT
    );

    glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(renderer.program);
    glBindVertexArray(renderer.vao);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 离屏渲染没有 eglSwapBuffers 的必要。
    // glReadPixels 会读取当前 read surface 的内容。
}

static void savePpm(
    const char* path,
    const std::vector<std::uint8_t>& rgba,
    int width,
    int height
) {
    FILE* file = nullptr;

#if defined(_MSC_VER)
    fopen_s(&file, path, "wb");
#else
    file = std::fopen(path, "wb");
#endif

    if (!file) {
        std::fprintf(stderr, "Failed to open output file: %s\n", path);
        std::exit(EXIT_FAILURE);
    }

    // PPM P6 是非常简单的无压缩 RGB 图片格式。
    // 很多图片查看器能打开，调试离屏渲染很方便。
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);

    // OpenGL 的 glReadPixels 原点在左下。
    // PPM 通常按从上到下写入，所以这里做 Y 翻转。
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            int index = (y * width + x) * 4;

            std::uint8_t rgb[3] = {
                rgba[index + 0],
                rgba[index + 1],
                rgba[index + 2]
            };

            std::fwrite(rgb, 1, 3, file);
        }
    }

    std::fclose(file);
}

static void readPixelsAndSave() {
    std::vector<std::uint8_t> rgba(
        PBUFFER_WIDTH * PBUFFER_HEIGHT * 4
    );

    glReadPixels(
        0,
        0,
        PBUFFER_WIDTH,
        PBUFFER_HEIGHT,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba.data()
    );

    const int centerX = PBUFFER_WIDTH / 2;
    const int centerY = PBUFFER_HEIGHT / 2;
    const int centerIndex =
        (centerY * PBUFFER_WIDTH + centerX) * 4;

    std::printf(
        "Center RGBA : %u %u %u %u\n",
        rgba[centerIndex + 0],
        rgba[centerIndex + 1],
        rgba[centerIndex + 2],
        rgba[centerIndex + 3]
    );

    const char* outputPath = "28-egl-pbuffer-output.ppm";

    savePpm(
        outputPath,
        rgba,
        PBUFFER_WIDTH,
        PBUFFER_HEIGHT
    );

    std::printf("Saved image : %s\n", outputPath);
}

static void destroyRenderer(Renderer& renderer) {
    glDeleteBuffers(1, &renderer.vbo);
    glDeleteVertexArrays(1, &renderer.vao);
    glDeleteProgram(renderer.program);
}

static void destroyEgl(EglObjects& egl) {
    checkEgl(
        eglMakeCurrent(
            egl.display,
            EGL_NO_SURFACE,
            EGL_NO_SURFACE,
            EGL_NO_CONTEXT
        ),
        "eglMakeCurrent cleanup"
    );

    checkEgl(
        eglDestroyContext(
            egl.display,
            egl.context
        ),
        "eglDestroyContext"
    );

    checkEgl(
        eglDestroySurface(
            egl.display,
            egl.surface
        ),
        "eglDestroySurface"
    );

    checkEgl(
        eglTerminate(egl.display),
        "eglTerminate"
    );

    eglReleaseThread();
}

int main() {
    EglObjects egl = createPbufferContext();
    Renderer renderer = createRenderer();

    renderToPbuffer(renderer);
    readPixelsAndSave();

    destroyRenderer(renderer);
    destroyEgl(egl);

    return EXIT_SUCCESS;
}