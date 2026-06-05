#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

// ============================================================
// 1. GPU 资源
// ============================================================

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
};

struct Renderer {
    GLuint program = 0;
    GLint modelLocation = -1;
    GLint viewLocation = -1;
    GLint projectionLocation = -1;
    GLint colorLocation = -1;

    Mesh cube;
    Mesh quad;
};

// ============================================================
// 2. 可以通过键盘切换的渲染状态
// ============================================================

struct RenderOptions {
    bool depthTest = true;
    bool blending = true;
    bool stencilOutline = true;
    bool culling = true;
    bool scissorTest = false;
};

static RenderOptions gOptions;

static void printOptions() {
    std::printf(
        "\nDepth[D]: %s | Blend[B]: %s | Outline[O]: %s | "
        "Cull[C]: %s | Scissor[S]: %s\n",
        gOptions.depthTest ? "ON" : "OFF",
        gOptions.blending ? "ON" : "OFF",
        gOptions.stencilOutline ? "ON" : "OFF",
        gOptions.culling ? "ON" : "OFF",
        gOptions.scissorTest ? "ON" : "OFF"
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

    switch (key) {
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;

    case GLFW_KEY_D:
        gOptions.depthTest = !gOptions.depthTest;
        break;

    case GLFW_KEY_B:
        gOptions.blending = !gOptions.blending;
        break;

    case GLFW_KEY_O:
        gOptions.stencilOutline = !gOptions.stencilOutline;
        break;

    case GLFW_KEY_C:
        gOptions.culling = !gOptions.culling;
        break;

    case GLFW_KEY_S:
        gOptions.scissorTest = !gOptions.scissorTest;
        break;

    default:
        return;
    }

    printOptions();
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
layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    gl_Position =
        uProjection *
        uView *
        uModel *
        vec4(aPosition, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

uniform vec4 uColor;

out vec4 fragColor;

void main() {
    fragColor = uColor;
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
// 4. Mesh
// ============================================================

static Mesh createMesh(
    const float* vertices,
    std::size_t vertexBytes,
    const unsigned short* indices,
    std::size_t indexBytes,
    GLsizei indexCount
) {
    Mesh mesh;
    mesh.indexCount = indexCount;

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        vertexBytes,
        vertices,
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        indexBytes,
        indices,
        GL_STATIC_DRAW
    );

    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        3 * sizeof(float),
        nullptr
    );

    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    return mesh;
}

static Mesh createCube() {
    const float vertices[] = {
        -0.5f, -0.5f, -0.5f, // 0
         0.5f, -0.5f, -0.5f, // 1
         0.5f,  0.5f, -0.5f, // 2
        -0.5f,  0.5f, -0.5f, // 3
        -0.5f, -0.5f,  0.5f, // 4
         0.5f, -0.5f,  0.5f, // 5
         0.5f,  0.5f,  0.5f, // 6
        -0.5f,  0.5f,  0.5f  // 7
    };

    // 从立方体外部观察时，三角形顶点采用逆时针顺序。
    const unsigned short indices[] = {
        4, 5, 6,  6, 7, 4, // 前
        1, 0, 3,  3, 2, 1, // 后
        0, 4, 7,  7, 3, 0, // 左
        5, 1, 2,  2, 6, 5, // 右
        3, 7, 6,  6, 2, 3, // 顶
        0, 1, 5,  5, 4, 0  // 底
    };

    return createMesh(
        vertices,
        sizeof(vertices),
        indices,
        sizeof(indices),
        36
    );
}

static Mesh createQuad() {
    // 位于 XY 平面，正面朝向 +Z。
    const float vertices[] = {
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.5f,  0.5f, 0.0f,
        -0.5f,  0.5f, 0.0f
    };

    const unsigned short indices[] = {
        0, 1, 2,
        2, 3, 0
    };

    return createMesh(
        vertices,
        sizeof(vertices),
        indices,
        sizeof(indices),
        6
    );
}

static Renderer createRenderer() {
    Renderer renderer;

    renderer.program = createProgram();

    renderer.modelLocation =
        glGetUniformLocation(renderer.program, "uModel");

    renderer.viewLocation =
        glGetUniformLocation(renderer.program, "uView");

    renderer.projectionLocation =
        glGetUniformLocation(renderer.program, "uProjection");

    renderer.colorLocation =
        glGetUniformLocation(renderer.program, "uColor");

    renderer.cube = createCube();
    renderer.quad = createQuad();

    return renderer;
}

// ============================================================
// 5. Draw Call 辅助函数
// ============================================================

static void drawMesh(
    const Renderer& renderer,
    const Mesh& mesh,
    const glm::mat4& model,
    const glm::vec4& color
) {
    glUniformMatrix4fv(
        renderer.modelLocation,
        1,
        GL_FALSE,
        glm::value_ptr(model)
    );

    glUniform4fv(
        renderer.colorLocation,
        1,
        glm::value_ptr(color)
    );

    glBindVertexArray(mesh.vao);

    glDrawElements(
        GL_TRIANGLES,
        mesh.indexCount,
        GL_UNSIGNED_SHORT,
        nullptr
    );
}

// ============================================================
// 6. 透明物体
// ============================================================

struct TransparentQuad {
    glm::vec3 position;
    glm::vec4 color;
};

static float distanceSquared(
    const glm::vec3& a,
    const glm::vec3& b
) {
    glm::vec3 delta = a - b;
    return glm::dot(delta, delta);
}

// ============================================================
// 7. 每帧渲染
// ============================================================

static void renderFrame(
    const Renderer& renderer,
    int width,
    int height,
    float elapsedSeconds
) {
    const glm::vec3 cameraPosition(0.0f, 1.3f, 5.0f);

    glm::mat4 view = glm::lookAt(
        cameraPosition,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    glm::mat4 projection = glm::perspective(
        glm::radians(60.0f),
        static_cast<float>(width) / static_cast<float>(height),
        0.1f,
        100.0f
    );

    // --------------------------------------------------------
    // 7.1 清屏
    // --------------------------------------------------------

    glViewport(0, 0, width, height);

    // Clear 也会受到 Scissor Test 和写入掩码影响。
    // 因此先恢复完整写入状态，再清理整个窗口。
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);

    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT |
        GL_STENCIL_BUFFER_BIT
    );

    // --------------------------------------------------------
    // 7.2 Scissor Test：限制后续绘制区域
    // --------------------------------------------------------

    if (gOptions.scissorTest) {
        glEnable(GL_SCISSOR_TEST);

        glScissor(
            width / 4,
            height / 4,
            width / 2,
            height / 2
        );
    }
    else {
        glDisable(GL_SCISSOR_TEST);
    }

    // --------------------------------------------------------
    // 7.3 深度测试
    // --------------------------------------------------------

    if (gOptions.depthTest) {
        glEnable(GL_DEPTH_TEST);

        // 新片元距离更近时通过深度测试。
        glDepthFunc(GL_LESS);
    }
    else {
        glDisable(GL_DEPTH_TEST);
    }

    // --------------------------------------------------------
    // 7.4 背面裁剪
    // --------------------------------------------------------

    if (gOptions.culling) {
        glEnable(GL_CULL_FACE);

        // 逆时针顶点顺序表示正面。
        glFrontFace(GL_CCW);

        // 跳过背向相机的三角形。
        glCullFace(GL_BACK);
    }
    else {
        glDisable(GL_CULL_FACE);
    }

    glUseProgram(renderer.program);

    glUniformMatrix4fv(
        renderer.viewLocation,
        1,
        GL_FALSE,
        glm::value_ptr(view)
    );

    glUniformMatrix4fv(
        renderer.projectionLocation,
        1,
        GL_FALSE,
        glm::value_ptr(projection)
    );

    // --------------------------------------------------------
    // 7.5 不透明物体：模板轮廓
    // --------------------------------------------------------

    glm::mat4 centerModel(1.0f);

    centerModel = glm::rotate(
        centerModel,
        elapsedSeconds * 0.7f,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    if (gOptions.stencilOutline) {
        glEnable(GL_STENCIL_TEST);

        // 模板测试始终通过。
        // 中间立方体通过深度测试后，将模板缓冲区写为 1。
        glStencilFunc(
            GL_ALWAYS,
            1,
            0xFF
        );

        glStencilOp(
            GL_KEEP,    // 模板测试失败
            GL_KEEP,    // 模板通过，但深度测试失败
            GL_REPLACE  // 模板和深度测试都通过
        );

        glStencilMask(0xFF);

        drawMesh(
            renderer,
            renderer.cube,
            centerModel,
            glm::vec4(0.20f, 0.65f, 1.00f, 1.0f)
        );

        // 绘制略微放大的立方体。
        // 仅允许模板值不等于 1 的片元通过，因此只剩外轮廓。
        glStencilFunc(
            GL_NOTEQUAL,
            1,
            0xFF
        );

        // 禁止修改模板缓冲区。
        glStencilMask(0x00);

        // 轮廓希望覆盖在物体边缘，不参与深度测试。
        glDisable(GL_DEPTH_TEST);

        glm::mat4 outlineModel =
            glm::scale(
                centerModel,
                glm::vec3(1.08f)
            );

        drawMesh(
            renderer,
            renderer.cube,
            outlineModel,
            glm::vec4(1.00f, 0.55f, 0.10f, 1.0f)
        );

        // 恢复后续绘制需要的状态。
        if (gOptions.depthTest) {
            glEnable(GL_DEPTH_TEST);
        }

        glStencilMask(0xFF);
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glDisable(GL_STENCIL_TEST);
    }
    else {
        glDisable(GL_STENCIL_TEST);

        drawMesh(
            renderer,
            renderer.cube,
            centerModel,
            glm::vec4(0.20f, 0.65f, 1.00f, 1.0f)
        );
    }

    // --------------------------------------------------------
    // 7.6 其他不透明物体
    // --------------------------------------------------------

    glm::mat4 leftModel(1.0f);

    leftModel = glm::translate(
        leftModel,
        glm::vec3(-1.45f, -0.15f, -0.75f)
    );

    leftModel = glm::rotate(
        leftModel,
        elapsedSeconds,
        glm::vec3(1.0f, 1.0f, 0.0f)
    );

    drawMesh(
        renderer,
        renderer.cube,
        leftModel,
        glm::vec4(0.90f, 0.25f, 0.30f, 1.0f)
    );

    glm::mat4 rightModel(1.0f);

    rightModel = glm::translate(
        rightModel,
        glm::vec3(1.35f, 0.10f, -1.10f)
    );

    rightModel = glm::rotate(
        rightModel,
        -elapsedSeconds * 0.8f,
        glm::vec3(0.0f, 1.0f, 1.0f)
    );

    drawMesh(
        renderer,
        renderer.cube,
        rightModel,
        glm::vec4(0.30f, 0.85f, 0.40f, 1.0f)
    );

    // --------------------------------------------------------
    // 7.7 半透明物体
    // --------------------------------------------------------

    if (gOptions.blending) {
        glEnable(GL_BLEND);

        // 常见 Alpha 混合公式：
        //
        // result =
        //     source.rgb * source.alpha +
        //     destination.rgb * (1 - source.alpha)
        glBlendEquation(GL_FUNC_ADD);

        glBlendFunc(
            GL_SRC_ALPHA,
            GL_ONE_MINUS_SRC_ALPHA
        );
    }
    else {
        glDisable(GL_BLEND);
    }

    // 透明物体通常仍然进行深度测试，
    // 但禁止写入深度缓冲区。
    //
    // 否则先绘制的透明平面可能阻止后面的透明平面参与混合。
    glDepthMask(GL_FALSE);

    // 为了简化透明 Quad 的观察，临时关闭背面裁剪。
    glDisable(GL_CULL_FACE);

    std::array<TransparentQuad, 3> transparentQuads = { {
        {
            glm::vec3(-0.35f, 0.05f, 1.15f),
            glm::vec4(1.00f, 0.25f, 0.20f, 0.42f)
        },
        {
            glm::vec3(0.20f, 0.20f, 0.35f),
            glm::vec4(0.25f, 1.00f, 0.35f, 0.42f)
        },
        {
            glm::vec3(0.45f, -0.05f, -0.45f),
            glm::vec4(0.30f, 0.50f, 1.00f, 0.42f)
        }
    } };

    // Alpha 混合与绘制顺序有关。
    // 常见做法是从远到近绘制透明物体。
    std::sort(
        transparentQuads.begin(),
        transparentQuads.end(),
        [&](const TransparentQuad& a, const TransparentQuad& b) {
            return distanceSquared(a.position, cameraPosition) >
                distanceSquared(b.position, cameraPosition);
        }
    );

    for (const TransparentQuad& quad : transparentQuads) {
        glm::mat4 model(1.0f);

        model = glm::translate(model, quad.position);

        model = glm::scale(
            model,
            glm::vec3(1.65f, 1.20f, 1.0f)
        );

        drawMesh(
            renderer,
            renderer.quad,
            model,
            quad.color
        );
    }

    // --------------------------------------------------------
    // 7.8 恢复状态
    // --------------------------------------------------------
    //
    // GLES 是状态机。状态会持续保留在 Context 中。
    // 一个渲染 Pass 修改状态后，应明确恢复或由下一个 Pass 完整设置。

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);

    if (gOptions.culling) {
        glEnable(GL_CULL_FACE);
    }
}

// ============================================================
// 8. 清理
// ============================================================

static void destroyMesh(Mesh& mesh) {
    glDeleteBuffers(1, &mesh.ebo);
    glDeleteBuffers(1, &mesh.vbo);
    glDeleteVertexArrays(1, &mesh.vao);
}

static void destroyRenderer(Renderer& renderer) {
    destroyMesh(renderer.quad);
    destroyMesh(renderer.cube);
    glDeleteProgram(renderer.program);
}

// ============================================================
// 9. 主函数
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

    // 模板轮廓实验需要默认 Framebuffer 提供模板缓冲区。
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    GLFWwindow* window = glfwCreateWindow(
        1000,
        720,
        "15 - Render States",
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

    GLint depthBits = 0;
    GLint stencilBits = 0;

    glGetIntegerv(GL_DEPTH_BITS, &depthBits);
    glGetIntegerv(GL_STENCIL_BITS, &stencilBits);

    std::printf("DEPTH_BITS  : %d\n", depthBits);
    std::printf("STENCIL_BITS: %d\n", stencilBits);

    std::printf("\nKeyboard controls:\n");
    std::printf("  D: depth test\n");
    std::printf("  B: alpha blending\n");
    std::printf("  O: stencil outline\n");
    std::printf("  C: back-face culling\n");
    std::printf("  S: scissor test\n");
    std::printf("  ESC: exit\n");

    printOptions();

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