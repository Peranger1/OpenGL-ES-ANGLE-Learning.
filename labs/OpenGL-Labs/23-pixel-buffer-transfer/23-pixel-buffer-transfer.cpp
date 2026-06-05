#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

constexpr int TEXTURE_WIDTH = 256;
constexpr int TEXTURE_HEIGHT = 256;
constexpr int CHANNELS = 4;
constexpr int UPLOAD_PBO_COUNT = 2;
constexpr int READBACK_PBO_COUNT = 2;

enum class UploadMode {
    DirectPixels,
    PixelUnpackBuffer
};

static UploadMode gUploadMode = UploadMode::PixelUnpackBuffer;
static bool gReadbackEnabled = true;

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
};

struct Renderer {
    GLuint program = 0;
    GLint textureLocation = -1;

    Mesh quad;

    GLuint texture = 0;

    GLuint uploadPbos[UPLOAD_PBO_COUNT]{};
    GLuint readbackPbos[READBACK_PBO_COUNT]{};

    std::vector<std::uint8_t> pixels;

    int frameIndex = 0;
};

static GLsizeiptr textureByteSize() {
    return TEXTURE_WIDTH * TEXTURE_HEIGHT * CHANNELS;
}

static void printMode() {
    std::printf(
        "Upload mode: %s, readback: %s\n",
        gUploadMode == UploadMode::DirectPixels
        ? "direct CPU pointer"
        : "GL_PIXEL_UNPACK_BUFFER",
        gReadbackEnabled ? "ON" : "OFF"
    );
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
        gUploadMode = UploadMode::DirectPixels;
        printMode();
        return;
    }

    if (key == GLFW_KEY_2) {
        gUploadMode = UploadMode::PixelUnpackBuffer;
        printMode();
        return;
    }

    if (key == GLFW_KEY_R) {
        gReadbackEnabled = !gReadbackEnabled;
        printMode();
        return;
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

static Mesh createQuad() {
    const float vertices[] = {
        // position       // uv
        -0.85f, -0.85f,   0.0f, 0.0f,
         0.85f, -0.85f,   1.0f, 0.0f,
         0.85f,  0.85f,   1.0f, 1.0f,
        -0.85f,  0.85f,   0.0f, 1.0f
    };

    const std::uint16_t indices[] = {
        0, 1, 2,
        2, 3, 0
    };

    Mesh mesh;

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
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

    return mesh;
}

static GLuint createTexture() {
    GLuint texture = 0;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        TEXTURE_WIDTH,
        TEXTURE_HEIGHT,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr
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
        GL_CLAMP_TO_EDGE
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE
    );

    glBindTexture(GL_TEXTURE_2D, 0);

    return texture;
}

static void generatePixels(
    std::vector<std::uint8_t>& pixels,
    float elapsedSeconds
) {
    for (int y = 0; y < TEXTURE_HEIGHT; ++y) {
        for (int x = 0; x < TEXTURE_WIDTH; ++x) {
            float u =
                static_cast<float>(x) /
                static_cast<float>(TEXTURE_WIDTH - 1);

            float v =
                static_cast<float>(y) /
                static_cast<float>(TEXTURE_HEIGHT - 1);

            float wave =
                std::sin(u * 24.0f + elapsedSeconds * 3.0f) *
                std::cos(v * 18.0f - elapsedSeconds * 2.0f);

            bool checker =
                ((x / 16) + (y / 16)) % 2 == 0;

            int offset = (y * TEXTURE_WIDTH + x) * CHANNELS;

            pixels[offset + 0] =
                static_cast<std::uint8_t>(
                    checker ? 240 : 40
                    );

            pixels[offset + 1] =
                static_cast<std::uint8_t>(
                    (wave * 0.5f + 0.5f) * 255.0f
                    );

            pixels[offset + 2] =
                static_cast<std::uint8_t>(
                    u * 255.0f
                    );

            pixels[offset + 3] = 255;
        }
    }
}

static void uploadTextureDirect(
    Renderer& renderer
) {
    // 确保没有 PIXEL_UNPACK_BUFFER 绑定。
    // 如果这里仍然绑定着 PBO，glTexSubImage2D 的最后一个参数会被解释为 PBO 内偏移。
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, renderer.texture);

    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        TEXTURE_WIDTH,
        TEXTURE_HEIGHT,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        renderer.pixels.data()
    );
}

static void uploadTextureWithPbo(
    Renderer& renderer
) {
    int pboIndex =
        renderer.frameIndex % UPLOAD_PBO_COUNT;

    GLuint pbo =
        renderer.uploadPbos[pboIndex];

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);

    // Orphaning：
    // 旧存储可能仍被 GPU 使用，传 nullptr 允许驱动换一块新存储。
    glBufferData(
        GL_PIXEL_UNPACK_BUFFER,
        textureByteSize(),
        nullptr,
        GL_STREAM_DRAW
    );

    void* mapped = glMapBufferRange(
        GL_PIXEL_UNPACK_BUFFER,
        0,
        textureByteSize(),
        GL_MAP_WRITE_BIT |
        GL_MAP_INVALIDATE_BUFFER_BIT
    );

    if (!mapped) {
        std::fprintf(stderr, "glMapBufferRange upload PBO failed\n");
        std::exit(EXIT_FAILURE);
    }

    std::memcpy(
        mapped,
        renderer.pixels.data(),
        static_cast<std::size_t>(textureByteSize())
    );

    if (glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER) != GL_TRUE) {
        std::fprintf(stderr, "glUnmapBuffer upload PBO failed\n");
        std::exit(EXIT_FAILURE);
    }

    glBindTexture(GL_TEXTURE_2D, renderer.texture);

    // 当 GL_PIXEL_UNPACK_BUFFER 被绑定时，最后一个参数不是 CPU 指针。
    // nullptr 表示从当前 PBO 的 offset 0 开始读取像素。
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        TEXTURE_WIDTH,
        TEXTURE_HEIGHT,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

static void uploadTexture(
    Renderer& renderer,
    float elapsedSeconds
) {
    generatePixels(
        renderer.pixels,
        elapsedSeconds
    );

    if (gUploadMode == UploadMode::DirectPixels) {
        uploadTextureDirect(renderer);
    }
    else {
        uploadTextureWithPbo(renderer);
    }
}

static void renderTexturedQuad(
    const Renderer& renderer,
    int width,
    int height
) {
    glViewport(0, 0, width, height);

    glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(renderer.program);
    glBindVertexArray(renderer.quad.vao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.texture);

    glDrawElements(
        GL_TRIANGLES,
        6,
        GL_UNSIGNED_SHORT,
        nullptr
    );
}

static void readbackCenterPixelWithPbo(
    Renderer& renderer,
    int width,
    int height
) {
    if (!gReadbackEnabled) {
        return;
    }

    constexpr GLsizeiptr readbackBytes = 4;

    int current =
        renderer.frameIndex % READBACK_PBO_COUNT;

    int previous =
        (renderer.frameIndex + READBACK_PBO_COUNT - 1) %
        READBACK_PBO_COUNT;

    // --------------------------------------------------------
    // 1. 本帧发起异步回读
    // --------------------------------------------------------

    glBindBuffer(
        GL_PIXEL_PACK_BUFFER,
        renderer.readbackPbos[current]
    );

    glBufferData(
        GL_PIXEL_PACK_BUFFER,
        readbackBytes,
        nullptr,
        GL_STREAM_READ
    );

    // 当 GL_PIXEL_PACK_BUFFER 被绑定时，最后一个参数表示 PBO 内偏移。
    // nullptr 表示把读回的 RGBA 像素写到 PBO offset 0。
    glReadPixels(
        width / 2,
        height / 2,
        1,
        1,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    // --------------------------------------------------------
    // 2. 读取上一帧 PBO 的结果
    // --------------------------------------------------------
    //
    // 真实项目中通常延迟一帧或多帧读取，减少 CPU 等待 GPU 的概率。

    if (renderer.frameIndex > 0 && renderer.frameIndex % 30 == 0) {
        glBindBuffer(
            GL_PIXEL_PACK_BUFFER,
            renderer.readbackPbos[previous]
        );

        void* mapped = glMapBufferRange(
            GL_PIXEL_PACK_BUFFER,
            0,
            readbackBytes,
            GL_MAP_READ_BIT
        );

        if (mapped) {
            const std::uint8_t* rgba =
                static_cast<const std::uint8_t*>(mapped);

            std::printf(
                "Center pixel RGBA: %3u %3u %3u %3u\n",
                rgba[0],
                rgba[1],
                rgba[2],
                rgba[3]
            );

            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

static Renderer createRenderer() {
    Renderer renderer;

    renderer.program = createProgram();
    renderer.textureLocation =
        glGetUniformLocation(renderer.program, "uTexture");

    renderer.quad = createQuad();
    renderer.texture = createTexture();

    renderer.pixels.resize(
        static_cast<std::size_t>(textureByteSize())
    );

    glGenBuffers(
        UPLOAD_PBO_COUNT,
        renderer.uploadPbos
    );

    glGenBuffers(
        READBACK_PBO_COUNT,
        renderer.readbackPbos
    );

    glUseProgram(renderer.program);

    // sampler2D 读取 GL_TEXTURE0。
    glUniform1i(
        renderer.textureLocation,
        0
    );

    return renderer;
}

static void renderFrame(
    Renderer& renderer,
    int width,
    int height,
    float elapsedSeconds
) {
    uploadTexture(
        renderer,
        elapsedSeconds
    );

    renderTexturedQuad(
        renderer,
        width,
        height
    );

    // 必须在 swap 之前回读当前后缓冲区内容。
    readbackCenterPixelWithPbo(
        renderer,
        width,
        height
    );

    ++renderer.frameIndex;
}

static void destroyRenderer(Renderer& renderer) {
    glDeleteBuffers(
        READBACK_PBO_COUNT,
        renderer.readbackPbos
    );

    glDeleteBuffers(
        UPLOAD_PBO_COUNT,
        renderer.uploadPbos
    );

    glDeleteTextures(1, &renderer.texture);

    glDeleteBuffers(1, &renderer.quad.ebo);
    glDeleteBuffers(1, &renderer.quad.vbo);
    glDeleteVertexArrays(1, &renderer.quad.vao);

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
        1000,
        720,
        "23 - Pixel Buffer Transfer",
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
    std::printf("  1: direct CPU pointer upload\n");
    std::printf("  2: GL_PIXEL_UNPACK_BUFFER upload\n");
    std::printf("  R: toggle PBO readback\n");
    std::printf("  ESC: exit\n\n");

    printMode();

    Renderer renderer = createRenderer();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int width = 0;
        int height = 0;

        glfwGetFramebufferSize(
            window,
            &width,
            &height
        );

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
