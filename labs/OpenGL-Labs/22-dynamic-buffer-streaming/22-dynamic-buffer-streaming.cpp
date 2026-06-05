#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

constexpr int VERTEX_COUNT = 512;
constexpr int RING_SLOT_COUNT = 3;

struct Vertex {
    float x;
    float y;
    float r;
    float g;
    float b;
};

enum class UploadMode {
    SubData = 0,
    OrphanSubData = 1,
    MapRingFence = 2
};

static UploadMode gUploadMode = UploadMode::SubData;

struct Renderer {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;

    GLint pointSizeLocation = -1;

    std::vector<Vertex> vertices;

    GLsync slotFences[RING_SLOT_COUNT]{};
    int frameIndex = 0;
};

static void printMode() {
    const char* name = "glBufferSubData";

    if (gUploadMode == UploadMode::OrphanSubData) {
        name = "orphan + glBufferSubData";
    }
    else if (gUploadMode == UploadMode::MapRingFence) {
        name = "ring + glMapBufferRange + fence";
    }

    std::printf("Upload mode: %s\n", name);
}

static void keyCallback(
    GLFWwindow* window,
    int key,
    int scanCode,
    int action,
    int modifiers
) {
    if (action != GLFW_PRESS) {
        return;
    }

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    if (key == GLFW_KEY_1) {
        gUploadMode = UploadMode::SubData;
        printMode();
    }

    if (key == GLFW_KEY_2) {
        gUploadMode = UploadMode::OrphanSubData;
        printMode();
    }

    if (key == GLFW_KEY_3) {
        gUploadMode = UploadMode::MapRingFence;
        printMode();
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
layout(location = 1) in vec3 aColor;

uniform float uPointSize;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_PointSize = uPointSize;
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

static void generateWaveVertices(
    std::vector<Vertex>& vertices,
    float elapsedSeconds
) {
    for (int i = 0; i < VERTEX_COUNT; ++i) {
        float t =
            static_cast<float>(i) /
            static_cast<float>(VERTEX_COUNT - 1);

        float x = t * 2.0f - 1.0f;

        float y =
            std::sin(t * 18.0f + elapsedSeconds * 2.0f) * 0.25f +
            std::sin(t * 47.0f - elapsedSeconds * 3.0f) * 0.08f;

        vertices[i].x = x;
        vertices[i].y = y;

        vertices[i].r = 0.25f + t * 0.75f;
        vertices[i].g = 0.75f;
        vertices[i].b = 1.0f - t * 0.65f;
    }
}

static void waitFenceOnCpu(GLsync fence) {
    if (!fence) {
        return;
    }

    while (true) {
        GLenum result = glClientWaitSync(
            fence,
            0,
            1'000'000 // 1 ms
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

static Renderer createRenderer() {
    Renderer renderer;

    renderer.program = createProgram();

    renderer.pointSizeLocation =
        glGetUniformLocation(renderer.program, "uPointSize");

    renderer.vertices.resize(VERTEX_COUNT);

    glGenVertexArrays(1, &renderer.vao);
    glGenBuffers(1, &renderer.vbo);

    glBindVertexArray(renderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);

    // 分配一个足够容纳 3 个 slot 的大 Buffer。
    //
    // 模式 1 和模式 2 只使用 offset = 0 的区域。
    // 模式 3 会在三个 slot 之间轮换。
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(Vertex) * VERTEX_COUNT * RING_SLOT_COUNT,
        nullptr,
        GL_STREAM_DRAW
    );

    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, x))
    );
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, r))
    );
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glUseProgram(renderer.program);
    glUniform1f(renderer.pointSizeLocation, 5.0f);

    return renderer;
}

static GLsizeiptr vertexBytes() {
    return sizeof(Vertex) * VERTEX_COUNT;
}

static GLintptr uploadWithSubData(Renderer& renderer) {
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);

    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        vertexBytes(),
        renderer.vertices.data()
    );

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return 0;
}

static GLintptr uploadWithOrphaning(Renderer& renderer) {
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);

    // Orphaning：
    // 重新调用 glBufferData，并传入 nullptr。
    //
    // 这表示应用不关心旧存储内容。
    // 驱动可以给你一块新存储，避免等待 GPU 读完旧数据。
    glBufferData(
        GL_ARRAY_BUFFER,
        vertexBytes() * RING_SLOT_COUNT,
        nullptr,
        GL_STREAM_DRAW
    );

    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        vertexBytes(),
        renderer.vertices.data()
    );

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return 0;
}

static GLintptr uploadWithMappedRing(Renderer& renderer) {
    int slotIndex =
        renderer.frameIndex % RING_SLOT_COUNT;

    GLsync& fence =
        renderer.slotFences[slotIndex];

    // 复用这个 slot 前，必须确认 GPU 已经不再读取它。
    waitFenceOnCpu(fence);

    if (fence) {
        glDeleteSync(fence);
        fence = nullptr;
    }

    GLintptr offset =
        static_cast<GLintptr>(slotIndex) * vertexBytes();

    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);

    void* mapped = glMapBufferRange(
        GL_ARRAY_BUFFER,
        offset,
        vertexBytes(),
        GL_MAP_WRITE_BIT |
        GL_MAP_INVALIDATE_RANGE_BIT
    );

    if (!mapped) {
        std::fprintf(stderr, "glMapBufferRange failed\n");
        std::exit(EXIT_FAILURE);
    }

    std::memcpy(
        mapped,
        renderer.vertices.data(),
        static_cast<std::size_t>(vertexBytes())
    );

    GLboolean unmapped =
        glUnmapBuffer(GL_ARRAY_BUFFER);

    if (unmapped != GL_TRUE) {
        std::fprintf(stderr, "glUnmapBuffer failed\n");
        std::exit(EXIT_FAILURE);
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return offset;
}

static GLintptr uploadDynamicVertices(
    Renderer& renderer,
    float elapsedSeconds
) {
    generateWaveVertices(
        renderer.vertices,
        elapsedSeconds
    );

    if (gUploadMode == UploadMode::SubData) {
        return uploadWithSubData(renderer);
    }

    if (gUploadMode == UploadMode::OrphanSubData) {
        return uploadWithOrphaning(renderer);
    }

    return uploadWithMappedRing(renderer);
}

static void configureVertexLayoutForOffset(
    const Renderer& renderer,
    GLintptr baseOffset
) {
    glBindVertexArray(renderer.vao);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);

    // VAO 记录 attribute 从 Buffer 的哪个偏移开始读取。
    // 环形缓冲区模式每帧会换 slot，因此这里每帧更新 offset。
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(
            baseOffset + offsetof(Vertex, x)
            )
    );

    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(
            baseOffset + offsetof(Vertex, r)
            )
    );
}

static void renderFrame(
    Renderer& renderer,
    int width,
    int height,
    float elapsedSeconds
) {
    GLintptr vertexOffset =
        uploadDynamicVertices(
            renderer,
            elapsedSeconds
        );

    glViewport(0, 0, width, height);

    glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(renderer.program);

    configureVertexLayoutForOffset(
        renderer,
        vertexOffset
    );

    // 画线，观察整体波形。
    glDrawArrays(
        GL_LINE_STRIP,
        0,
        VERTEX_COUNT
    );

    // 再画点，观察每个动态顶点。
    glDrawArrays(
        GL_POINTS,
        0,
        VERTEX_COUNT
    );

    if (gUploadMode == UploadMode::MapRingFence) {
        int slotIndex =
            renderer.frameIndex % RING_SLOT_COUNT;

        // Fence 放在 Draw Call 之后。
        // 当这个 Fence 触发时，说明 GPU 已经用完当前 slot 的顶点数据。
        renderer.slotFences[slotIndex] =
            glFenceSync(
                GL_SYNC_GPU_COMMANDS_COMPLETE,
                0
            );

        // 推进命令，避免后续 CPU 等待这个 Fence 时命令还停在驱动队列里。
        glFlush();
    }

    ++renderer.frameIndex;
}

static void destroyRenderer(Renderer& renderer) {
    for (GLsync& fence : renderer.slotFences) {
        waitFenceOnCpu(fence);

        if (fence) {
            glDeleteSync(fence);
            fence = nullptr;
        }
    }

    glDeleteBuffers(1, &renderer.vbo);
    glDeleteVertexArrays(1, &renderer.vao);
    glDeleteProgram(renderer.program);
}

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(
        1100,
        700,
        "22 - Dynamic Buffer Streaming",
        nullptr,
        nullptr
    );

    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetKeyCallback(window, keyCallback);

    std::printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));

    std::printf("\nKeyboard controls:\n");
    std::printf("  1: glBufferSubData\n");
    std::printf("  2: orphan + glBufferSubData\n");
    std::printf("  3: ring + glMapBufferRange + fence\n");
    std::printf("  ESC: exit\n");

    printMode();

    Renderer renderer = createRenderer();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int width = 0;
        int height = 0;

        glfwGetFramebufferSize(window, &width, &height);

        if (width > 0 && height > 0) {
            renderFrame(
                renderer,
                width,
                height,
                static_cast<float>(glfwGetTime())
            );

            glfwSwapBuffers(window);
        }
    }

    destroyRenderer(renderer);

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}