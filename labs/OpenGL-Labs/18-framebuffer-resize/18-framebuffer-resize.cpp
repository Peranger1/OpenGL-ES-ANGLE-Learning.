#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

// ============================================================
// 1. 基础 GPU 对象
// ============================================================

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
};

struct SceneProgram {
    GLuint program = 0;
    GLint modelLocation = -1;
    GLint viewLocation = -1;
    GLint projectionLocation = -1;
    GLint colorLocation = -1;
};

struct ScreenProgram {
    GLuint program = 0;
    GLint textureLocation = -1;
};

struct RenderTarget {
    GLuint framebuffer = 0;
    GLuint colorTexture = 0;
    GLuint depthRenderbuffer = 0;
    int width = 0;
    int height = 0;
};

struct Renderer {
    SceneProgram sceneProgram;
    ScreenProgram screenProgram;

    Mesh cube;
    Mesh screenQuad;

    RenderTarget offscreen;
};

// ============================================================
// 2. Shader 编译
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

static GLuint linkProgram(
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
        char log[2048]{};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);

        std::fprintf(stderr, "program link failed:\n%s\n", log);
        std::exit(EXIT_FAILURE);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

static SceneProgram createSceneProgram() {
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

    SceneProgram sceneProgram;
    sceneProgram.program =
        linkProgram(vertexSource, fragmentSource);

    sceneProgram.modelLocation =
        glGetUniformLocation(sceneProgram.program, "uModel");

    sceneProgram.viewLocation =
        glGetUniformLocation(sceneProgram.program, "uView");

    sceneProgram.projectionLocation =
        glGetUniformLocation(sceneProgram.program, "uProjection");

    sceneProgram.colorLocation =
        glGetUniformLocation(sceneProgram.program, "uColor");

    return sceneProgram;
}

static ScreenProgram createScreenProgram() {
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

uniform sampler2D uSceneTexture;

out vec4 fragColor;

void main() {
    vec3 color = texture(uSceneTexture, vTexCoord).rgb;

    // 做一个很轻的后处理：边缘暗角。
    vec2 centered = vTexCoord * 2.0 - 1.0;
    float vignette =
        1.0 - smoothstep(0.45, 1.35, length(centered));

    fragColor = vec4(color * vignette, 1.0);
}
)";

    ScreenProgram screenProgram;
    screenProgram.program =
        linkProgram(vertexSource, fragmentSource);

    screenProgram.textureLocation =
        glGetUniformLocation(screenProgram.program, "uSceneTexture");

    glUseProgram(screenProgram.program);

    // sampler2D 保存纹理单元编号。
    glUniform1i(screenProgram.textureLocation, 0);

    return screenProgram;
}

// ============================================================
// 3. Mesh
// ============================================================

static Mesh createCubeMesh() {
    const float vertices[] = {
        -0.5f, -0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,
        -0.5f,  0.5f, -0.5f,

        -0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f
    };

    const std::uint16_t indices[] = {
        4, 5, 6,  6, 7, 4,
        1, 0, 3,  3, 2, 1,
        0, 4, 7,  7, 3, 0,
        5, 1, 2,  2, 6, 5,
        3, 7, 6,  6, 2, 3,
        0, 1, 5,  5, 4, 0
    };

    Mesh mesh;
    mesh.indexCount = 36;

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

static Mesh createScreenQuad() {
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
    mesh.indexCount = 6;

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
// 4. FBO 创建、重建与检查
// ============================================================

static const char* framebufferStatusName(GLenum status) {
    switch (status) {
    case GL_FRAMEBUFFER_COMPLETE:
        return "GL_FRAMEBUFFER_COMPLETE";

    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";

    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";

    case GL_FRAMEBUFFER_UNSUPPORTED:
        return "GL_FRAMEBUFFER_UNSUPPORTED";

    default:
        return "Unknown framebuffer status";
    }
}

static void destroyRenderTargetAttachments(RenderTarget& target) {
    if (target.depthRenderbuffer != 0) {
        glDeleteRenderbuffers(1, &target.depthRenderbuffer);
        target.depthRenderbuffer = 0;
    }

    if (target.colorTexture != 0) {
        glDeleteTextures(1, &target.colorTexture);
        target.colorTexture = 0;
    }

    target.width = 0;
    target.height = 0;
}

static void createOrResizeRenderTarget(
    RenderTarget& target,
    int width,
    int height
) {
    if (width <= 0 || height <= 0) {
        return;
    }

    if (
        target.framebuffer != 0 &&
        target.width == width &&
        target.height == height
        ) {
        return;
    }

    if (target.framebuffer == 0) {
        glGenFramebuffers(1, &target.framebuffer);
    }

    destroyRenderTargetAttachments(target);

    target.width = width;
    target.height = height;

    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);

    // --------------------------------------------------------
    // 4.1 Color Attachment：用 Texture 保存颜色结果
    // --------------------------------------------------------

    glGenTextures(1, &target.colorTexture);
    glBindTexture(GL_TEXTURE_2D, target.colorTexture);

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

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        target.colorTexture,
        0
    );

    // --------------------------------------------------------
    // 4.2 Depth Attachment：用 Renderbuffer 保存深度
    // --------------------------------------------------------
    //
    // 如果深度结果不需要在 Shader 中采样，Renderbuffer 很合适。
    // 如果后续要做 Shadow Map 或深度可视化，就应该使用 Depth Texture。

    glGenRenderbuffers(1, &target.depthRenderbuffer);
    glBindRenderbuffer(
        GL_RENDERBUFFER,
        target.depthRenderbuffer
    );

    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH_COMPONENT24,
        width,
        height
    );

    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER,
        target.depthRenderbuffer
    );

    GLenum status =
        glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(
            stderr,
            "Framebuffer incomplete: %s (0x%04X)\n",
            framebufferStatusName(status),
            status
        );

        std::exit(EXIT_FAILURE);
    }

    std::printf(
        "RenderTarget resized: %d x %d\n",
        width,
        height
    );

    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void destroyRenderTarget(RenderTarget& target) {
    destroyRenderTargetAttachments(target);

    if (target.framebuffer != 0) {
        glDeleteFramebuffers(1, &target.framebuffer);
        target.framebuffer = 0;
    }
}

// ============================================================
// 5. Renderer
// ============================================================

static Renderer createRenderer() {
    Renderer renderer;

    renderer.sceneProgram = createSceneProgram();
    renderer.screenProgram = createScreenProgram();

    renderer.cube = createCubeMesh();
    renderer.screenQuad = createScreenQuad();

    glEnable(GL_DEPTH_TEST);

    return renderer;
}

// ============================================================
// 6. 第一遍：渲染到离屏 FBO
// ============================================================

static void renderSceneToTarget(
    const Renderer& renderer,
    const RenderTarget& target,
    float elapsedSeconds
) {
    glBindFramebuffer(
        GL_FRAMEBUFFER,
        target.framebuffer
    );

    glViewport(0, 0, target.width, target.height);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT
    );

    glm::vec3 cameraPosition(0.0f, 1.2f, 4.4f);

    glm::mat4 view = glm::lookAt(
        cameraPosition,
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    glm::mat4 projection = glm::perspective(
        glm::radians(60.0f),
        static_cast<float>(target.width) /
        static_cast<float>(target.height),
        0.1f,
        100.0f
    );

    glUseProgram(renderer.sceneProgram.program);

    glUniformMatrix4fv(
        renderer.sceneProgram.viewLocation,
        1,
        GL_FALSE,
        glm::value_ptr(view)
    );

    glUniformMatrix4fv(
        renderer.sceneProgram.projectionLocation,
        1,
        GL_FALSE,
        glm::value_ptr(projection)
    );

    glBindVertexArray(renderer.cube.vao);

    for (int i = 0; i < 3; ++i) {
        float x = static_cast<float>(i - 1) * 1.25f;

        glm::mat4 model(1.0f);

        model = glm::translate(
            model,
            glm::vec3(x, 0.0f, 0.0f)
        );

        model = glm::rotate(
            model,
            elapsedSeconds * (0.7f + i * 0.2f),
            glm::normalize(glm::vec3(1.0f, 1.0f + i, 0.4f))
        );

        glm::vec4 color(
            i == 0 ? 0.95f : 0.25f,
            i == 1 ? 0.85f : 0.35f,
            i == 2 ? 1.00f : 0.45f,
            1.0f
        );

        glUniformMatrix4fv(
            renderer.sceneProgram.modelLocation,
            1,
            GL_FALSE,
            glm::value_ptr(model)
        );

        glUniform4fv(
            renderer.sceneProgram.colorLocation,
            1,
            glm::value_ptr(color)
        );

        glDrawElements(
            GL_TRIANGLES,
            renderer.cube.indexCount,
            GL_UNSIGNED_SHORT,
            nullptr
        );
    }
}

// ============================================================
// 7. 第二遍：把离屏颜色纹理显示到窗口
// ============================================================

static void renderTargetToScreen(
    const Renderer& renderer,
    const RenderTarget& target,
    int windowWidth,
    int windowHeight
) {
    // 绑定 0 表示绘制到默认 Framebuffer，也就是窗口后缓冲区。
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(0, 0, windowWidth, windowHeight);

    glDisable(GL_DEPTH_TEST);

    glClearColor(0.02f, 0.025f, 0.035f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(renderer.screenProgram.program);
    glBindVertexArray(renderer.screenQuad.vao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, target.colorTexture);

    glDrawElements(
        GL_TRIANGLES,
        renderer.screenQuad.indexCount,
        GL_UNSIGNED_SHORT,
        nullptr
    );

    glBindTexture(GL_TEXTURE_2D, 0);
}

// ============================================================
// 8. 每帧渲染
// ============================================================

static void renderFrame(
    Renderer& renderer,
    int windowWidth,
    int windowHeight,
    float elapsedSeconds
) {
    createOrResizeRenderTarget(
        renderer.offscreen,
        windowWidth,
        windowHeight
    );

    renderSceneToTarget(
        renderer,
        renderer.offscreen,
        elapsedSeconds
    );

    renderTargetToScreen(
        renderer,
        renderer.offscreen,
        windowWidth,
        windowHeight
    );
}

// ============================================================
// 9. 清理
// ============================================================

static void destroyMesh(Mesh& mesh) {
    glDeleteBuffers(1, &mesh.ebo);
    glDeleteBuffers(1, &mesh.vbo);
    glDeleteVertexArrays(1, &mesh.vao);
}

static void destroyRenderer(Renderer& renderer) {
    destroyRenderTarget(renderer.offscreen);

    destroyMesh(renderer.screenQuad);
    destroyMesh(renderer.cube);

    glDeleteProgram(renderer.screenProgram.program);
    glDeleteProgram(renderer.sceneProgram.program);
}

// ============================================================
// 10. 主函数
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

    glfwWindowHint(GLFW_DEPTH_BITS, 24);

    GLFWwindow* window = glfwCreateWindow(
        1000,
        720,
        "18 - Framebuffer Resize",
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

    std::printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
    std::printf("Resize the window to recreate FBO attachments.\n");

    Renderer renderer = createRenderer();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

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