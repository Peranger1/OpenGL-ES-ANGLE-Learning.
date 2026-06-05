#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// ============================================================
// 1. GPU 对象
// ============================================================

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
};

struct Samplers {
    GLuint nearestRepeat = 0;
    GLuint linearRepeat = 0;
    GLuint mipmapRepeat = 0;
    GLuint mipmapClamp = 0;
};

struct Renderer {
    GLuint program = 0;

    GLint offsetLocation = -1;
    GLint scaleLocation = -1;
    GLint uvScaleLocation = -1;
    GLint textureLocation = -1;

    Mesh quad;
    GLuint texture = 0;
    Samplers samplers;
};

// ============================================================
// 2. 交互参数
// ============================================================

static float gUvScale = 16.0f;

static void printHelp() {
    std::printf("\nKeyboard controls:\n");
    std::printf("  1: UV scale = 2\n");
    std::printf("  2: UV scale = 16\n");
    std::printf("  3: UV scale = 64\n");
    std::printf("  ESC: exit\n");
    std::printf("\nCurrent UV scale: %.1f\n", gUvScale);
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

    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;

    case GLFW_KEY_1:
        gUvScale = 2.0f;
        break;

    case GLFW_KEY_2:
        gUvScale = 16.0f;
        break;

    case GLFW_KEY_3:
        gUvScale = 64.0f;
        break;

    default:
        return;
    }

    std::printf("Current UV scale: %.1f\n", gUvScale);
}

// ============================================================
// 3. Shader
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

uniform vec2 uOffset;
uniform vec2 uScale;
uniform float uUvScale;

out vec2 vTexCoord;

void main() {
    vec2 position = aPosition * uScale + uOffset;

    gl_Position = vec4(position, 0.0, 1.0);

    // UV 缩放越大，纹理在同一块屏幕区域内重复越多。
    // 当重复次数很高时，纹理会发生 minification，此时 mipmap 更重要。
    vTexCoord = aTexCoord * uUvScale;
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec2 vTexCoord;

uniform sampler2D uTexture;

out vec4 fragColor;

void main() {
    // texture() 会根据当前 Texture Unit 上绑定的 Texture Object，
    // 以及当前 Texture Unit 上绑定的 Sampler Object 来采样。
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

// ============================================================
// 4. Quad Mesh
// ============================================================

static Mesh createQuad() {
    // 一个标准矩形。
    // position 是裁剪空间附近的局部坐标。
    // uv 是 0 到 1，实际重复次数由 Shader 中的 uUvScale 控制。
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

// ============================================================
// 5. Texture Object
// ============================================================

static GLuint createCheckerTexture() {
    constexpr int width = 256;
    constexpr int height = 256;

    std::vector<std::uint8_t> pixels(width * height * 4);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int index = (y * width + x) * 4;

            // 高频棋盘格，让过滤和 mipmap 差异更明显。
            bool smallChecker =
                ((x / 4) + (y / 4)) % 2 == 0;

            bool largeChecker =
                ((x / 32) + (y / 32)) % 2 == 0;

            pixels[index + 0] = smallChecker ? 245 : 35;
            pixels[index + 1] = largeChecker ? 215 : 60;
            pixels[index + 2] = static_cast<std::uint8_t>(x);
            pixels[index + 3] = 255;
        }
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);

    glBindTexture(GL_TEXTURE_2D, texture);

    // Texture Object 保存图像数据。
    // 这里上传 level 0，也就是最原始、最高分辨率的图像。
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels.data()
    );

    // 根据 level 0 自动生成更小的 mipmap 层级。
    //
    // 如果某个 Sampler 使用 GL_LINEAR_MIPMAP_LINEAR，
    // Texture Object 必须拥有完整 mipmap，否则纹理会变成 incomplete。
    glGenerateMipmap(GL_TEXTURE_2D);

    // 这些参数也可以设置在 Texture Object 上。
    // 但是本实验后面会绑定 Sampler Object。
    // 当 Texture Unit 同时绑定 Texture 和 Sampler 时，采样规则来自 Sampler。
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_LINEAR_MIPMAP_LINEAR
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

    glBindTexture(GL_TEXTURE_2D, 0);

    return texture;
}

// ============================================================
// 6. Sampler Object
// ============================================================

static GLuint createSampler(
    GLint minFilter,
    GLint magFilter,
    GLint wrapS,
    GLint wrapT
) {
    GLuint sampler = 0;
    glGenSamplers(1, &sampler);

    // Sampler Object 保存采样规则，不保存图像数据。
    glSamplerParameteri(
        sampler,
        GL_TEXTURE_MIN_FILTER,
        minFilter
    );

    glSamplerParameteri(
        sampler,
        GL_TEXTURE_MAG_FILTER,
        magFilter
    );

    glSamplerParameteri(
        sampler,
        GL_TEXTURE_WRAP_S,
        wrapS
    );

    glSamplerParameteri(
        sampler,
        GL_TEXTURE_WRAP_T,
        wrapT
    );

    return sampler;
}

static Samplers createSamplers() {
    Samplers samplers;

    // 最近邻采样：像素块感最明显。
    samplers.nearestRepeat = createSampler(
        GL_NEAREST,
        GL_NEAREST,
        GL_REPEAT,
        GL_REPEAT
    );

    // 线性采样：放大时更平滑，但缩小时仍可能闪烁。
    samplers.linearRepeat = createSampler(
        GL_LINEAR,
        GL_LINEAR,
        GL_REPEAT,
        GL_REPEAT
    );

    // 三线性 mipmap：缩小时在 mipmap 层级之间平滑过渡。
    samplers.mipmapRepeat = createSampler(
        GL_LINEAR_MIPMAP_LINEAR,
        GL_LINEAR,
        GL_REPEAT,
        GL_REPEAT
    );

    // 同样使用 mipmap，但边界使用 clamp。
    // 当 UV 超过 0 到 1 时，不再重复，而是拉伸边缘像素。
    samplers.mipmapClamp = createSampler(
        GL_LINEAR_MIPMAP_LINEAR,
        GL_LINEAR,
        GL_CLAMP_TO_EDGE,
        GL_CLAMP_TO_EDGE
    );

    return samplers;
}

// ============================================================
// 7. Renderer
// ============================================================

static Renderer createRenderer() {
    Renderer renderer;

    renderer.program = createProgram();

    renderer.offsetLocation =
        glGetUniformLocation(renderer.program, "uOffset");

    renderer.scaleLocation =
        glGetUniformLocation(renderer.program, "uScale");

    renderer.uvScaleLocation =
        glGetUniformLocation(renderer.program, "uUvScale");

    renderer.textureLocation =
        glGetUniformLocation(renderer.program, "uTexture");

    renderer.quad = createQuad();
    renderer.texture = createCheckerTexture();
    renderer.samplers = createSamplers();

    glUseProgram(renderer.program);

    // sampler2D uniform 保存的是 Texture Unit 的索引。
    // 0 表示从 GL_TEXTURE0 这个纹理单元读取。
    glUniform1i(renderer.textureLocation, 0);

    return renderer;
}

// ============================================================
// 8. 绘制一个面板
// ============================================================

static void drawPanel(
    const Renderer& renderer,
    GLuint sampler,
    float offsetX,
    float offsetY,
    float scaleX,
    float scaleY
) {
    glUseProgram(renderer.program);

    glUniform2f(
        renderer.offsetLocation,
        offsetX,
        offsetY
    );

    glUniform2f(
        renderer.scaleLocation,
        scaleX,
        scaleY
    );

    glUniform1f(
        renderer.uvScaleLocation,
        gUvScale
    );

    glBindVertexArray(renderer.quad.vao);

    // Texture Unit 是 Shader 与纹理之间的槽位。
    //
    // 下面两行的含义是：
    //   GL_TEXTURE0 这个纹理单元上
    //   绑定 renderer.texture 作为图像来源
    //   绑定 sampler 作为采样规则
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer.texture);
    glBindSampler(0, sampler);

    glDrawElements(
        GL_TRIANGLES,
        6,
        GL_UNSIGNED_SHORT,
        nullptr
    );
}

// ============================================================
// 9. 每帧渲染
// ============================================================

static void renderFrame(
    const Renderer& renderer,
    int width,
    int height
) {
    glViewport(0, 0, width, height);

    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 四个面板使用同一张 Texture Object。
    // 差异只来自 Sampler Object。
    //
    // 从左到右：
    //   1. nearest + repeat
    //   2. linear + repeat
    //   3. mipmap + repeat
    //   4. mipmap + clamp
    drawPanel(
        renderer,
        renderer.samplers.nearestRepeat,
        -0.75f,
        0.0f,
        0.20f,
        0.72f
    );

    drawPanel(
        renderer,
        renderer.samplers.linearRepeat,
        -0.25f,
        0.0f,
        0.20f,
        0.72f
    );

    drawPanel(
        renderer,
        renderer.samplers.mipmapRepeat,
        0.25f,
        0.0f,
        0.20f,
        0.72f
    );

    drawPanel(
        renderer,
        renderer.samplers.mipmapClamp,
        0.75f,
        0.0f,
        0.20f,
        0.72f
    );

    // 解除 Sampler 绑定不是必须的，但演示状态恢复。
    glBindSampler(0, 0);
}

// ============================================================
// 10. 清理
// ============================================================

static void destroyRenderer(Renderer& renderer) {
    glDeleteSamplers(1, &renderer.samplers.mipmapClamp);
    glDeleteSamplers(1, &renderer.samplers.mipmapRepeat);
    glDeleteSamplers(1, &renderer.samplers.linearRepeat);
    glDeleteSamplers(1, &renderer.samplers.nearestRepeat);

    glDeleteTextures(1, &renderer.texture);

    glDeleteBuffers(1, &renderer.quad.ebo);
    glDeleteBuffers(1, &renderer.quad.vbo);
    glDeleteVertexArrays(1, &renderer.quad.vao);

    glDeleteProgram(renderer.program);
}

// ============================================================
// 11. 主函数
// ============================================================

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
        "16 - Texture Sampler Mipmap",
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

    GLint textureUnits = 0;
    GLint maxTextureSize = 0;
    GLint maxCombinedTextureUnits = 0;

    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &textureUnits);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    glGetIntegerv(
        GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
        &maxCombinedTextureUnits
    );

    std::printf("GL_MAX_TEXTURE_IMAGE_UNITS          : %d\n", textureUnits);
    std::printf("GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS : %d\n",
        maxCombinedTextureUnits);
    std::printf("GL_MAX_TEXTURE_SIZE                 : %d\n", maxTextureSize);

    printHelp();

    Renderer renderer = createRenderer();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int width = 0;
        int height = 0;

        glfwGetFramebufferSize(window, &width, &height);

        if (width > 0 && height > 0) {
            renderFrame(renderer, width, height);
            glfwSwapBuffers(window);
        }
    }

    destroyRenderer(renderer);

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}