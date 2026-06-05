#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// MRT：一次 Draw Call 同时输出 Color 和 Normal 两张图
// MSAA：先渲染到多采样 Renderbuffer，再 Resolve 到普通 Texture

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
    GLint normalMatrixLocation = -1;
    GLint colorLocation = -1;
};

struct ScreenProgram {
    GLuint program = 0;
    GLint offsetLocation = -1;
    GLint scaleLocation = -1;
    GLint textureLocation = -1;
};

struct MrtMsaaTarget {
    int width = 0;
    int height = 0;
    int samples = 1;

    GLuint msaaFramebuffer = 0;
    GLuint msaaColorRenderbuffer = 0;
    GLuint msaaNormalRenderbuffer = 0;
    GLuint msaaDepthRenderbuffer = 0;

    GLuint resolveFramebuffer = 0;
    GLuint resolvedColorTexture = 0;
    GLuint resolvedNormalTexture = 0;
};

struct Renderer {
    SceneProgram sceneProgram;
    ScreenProgram screenProgram;
    Mesh cube;
    Mesh screenQuad;
    MrtMsaaTarget target;
};

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

static GLuint linkProgram(const char* vertexSource, const char* fragmentSource) {
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
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out vec3 vWorldNormal;

void main() {
    vWorldNormal = normalize(uNormalMatrix * aNormal);

    gl_Position =
        uProjection *
        uView *
        uModel *
        vec4(aPosition, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec3 vWorldNormal;

uniform vec3 uColor;

// MRT：Fragment Shader 可以声明多个输出。
// location = 0 写入 GL_COLOR_ATTACHMENT0。
// location = 1 写入 GL_COLOR_ATTACHMENT1。
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;

void main() {
    vec3 normal = normalize(vWorldNormal);

    vec3 lightDirection = normalize(vec3(-0.4, 0.8, 0.5));
    float diffuse = max(dot(normal, lightDirection), 0.0);

    vec3 color = uColor * (0.25 + diffuse * 0.75);

    outColor = vec4(color, 1.0);

    // 法线有负值，不能直接保存到普通 RGBA8 颜色纹理。
    // 映射到 0..1 后方便显示。
    outNormal = vec4(normal * 0.5 + 0.5, 1.0);
}
)";

    SceneProgram result;
    result.program = linkProgram(vertexSource, fragmentSource);

    result.modelLocation =
        glGetUniformLocation(result.program, "uModel");
    result.viewLocation =
        glGetUniformLocation(result.program, "uView");
    result.projectionLocation =
        glGetUniformLocation(result.program, "uProjection");
    result.normalMatrixLocation =
        glGetUniformLocation(result.program, "uNormalMatrix");
    result.colorLocation =
        glGetUniformLocation(result.program, "uColor");

    return result;
}

static ScreenProgram createScreenProgram() {
    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;

uniform vec2 uOffset;
uniform vec2 uScale;

out vec2 vTexCoord;

void main() {
    vec2 position = aPosition * uScale + uOffset;
    gl_Position = vec4(position, 0.0, 1.0);
    vTexCoord = aTexCoord;
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

    ScreenProgram result;
    result.program = linkProgram(vertexSource, fragmentSource);

    result.offsetLocation =
        glGetUniformLocation(result.program, "uOffset");
    result.scaleLocation =
        glGetUniformLocation(result.program, "uScale");
    result.textureLocation =
        glGetUniformLocation(result.program, "uTexture");

    glUseProgram(result.program);
    glUniform1i(result.textureLocation, 0);

    return result;
}

static Mesh createCubeMesh() {
    const float vertices[] = {
        // position              // normal
        -0.5f,-0.5f, 0.5f,       0.0f, 0.0f, 1.0f,
         0.5f,-0.5f, 0.5f,       0.0f, 0.0f, 1.0f,
         0.5f, 0.5f, 0.5f,       0.0f, 0.0f, 1.0f,
        -0.5f, 0.5f, 0.5f,       0.0f, 0.0f, 1.0f,

         0.5f,-0.5f,-0.5f,       0.0f, 0.0f,-1.0f,
        -0.5f,-0.5f,-0.5f,       0.0f, 0.0f,-1.0f,
        -0.5f, 0.5f,-0.5f,       0.0f, 0.0f,-1.0f,
         0.5f, 0.5f,-0.5f,       0.0f, 0.0f,-1.0f,

        -0.5f,-0.5f,-0.5f,      -1.0f, 0.0f, 0.0f,
        -0.5f,-0.5f, 0.5f,      -1.0f, 0.0f, 0.0f,
        -0.5f, 0.5f, 0.5f,      -1.0f, 0.0f, 0.0f,
        -0.5f, 0.5f,-0.5f,      -1.0f, 0.0f, 0.0f,

         0.5f,-0.5f, 0.5f,       1.0f, 0.0f, 0.0f,
         0.5f,-0.5f,-0.5f,       1.0f, 0.0f, 0.0f,
         0.5f, 0.5f,-0.5f,       1.0f, 0.0f, 0.0f,
         0.5f, 0.5f, 0.5f,       1.0f, 0.0f, 0.0f,

        -0.5f, 0.5f, 0.5f,       0.0f, 1.0f, 0.0f,
         0.5f, 0.5f, 0.5f,       0.0f, 1.0f, 0.0f,
         0.5f, 0.5f,-0.5f,       0.0f, 1.0f, 0.0f,
        -0.5f, 0.5f,-0.5f,       0.0f, 1.0f, 0.0f,

        -0.5f,-0.5f,-0.5f,       0.0f,-1.0f, 0.0f,
         0.5f,-0.5f,-0.5f,       0.0f,-1.0f, 0.0f,
         0.5f,-0.5f, 0.5f,       0.0f,-1.0f, 0.0f,
        -0.5f,-0.5f, 0.5f,       0.0f,-1.0f, 0.0f
    };

    const std::uint16_t indices[] = {
         0, 1, 2,   2, 3, 0,
         4, 5, 6,   6, 7, 4,
         8, 9,10,  10,11, 8,
        12,13,14,  14,15,12,
        16,17,18,  18,19,16,
        20,21,22,  22,23,20
    };

    Mesh mesh;
    mesh.indexCount = 36;

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        6 * sizeof(float),
        nullptr
    );
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        6 * sizeof(float),
        reinterpret_cast<void*>(3 * sizeof(float))
    );
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    return mesh;
}

static Mesh createScreenQuad() {
    const float vertices[] = {
        // position       // uv
        -1.0f,-1.0f,      0.0f, 0.0f,
         1.0f,-1.0f,      1.0f, 0.0f,
         1.0f, 1.0f,      1.0f, 1.0f,
        -1.0f, 1.0f,      0.0f, 1.0f
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
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

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

static void deleteTargetAttachments(MrtMsaaTarget& target) {
    if (target.msaaDepthRenderbuffer) {
        glDeleteRenderbuffers(1, &target.msaaDepthRenderbuffer);
        target.msaaDepthRenderbuffer = 0;
    }

    if (target.msaaNormalRenderbuffer) {
        glDeleteRenderbuffers(1, &target.msaaNormalRenderbuffer);
        target.msaaNormalRenderbuffer = 0;
    }

    if (target.msaaColorRenderbuffer) {
        glDeleteRenderbuffers(1, &target.msaaColorRenderbuffer);
        target.msaaColorRenderbuffer = 0;
    }

    if (target.resolvedNormalTexture) {
        glDeleteTextures(1, &target.resolvedNormalTexture);
        target.resolvedNormalTexture = 0;
    }

    if (target.resolvedColorTexture) {
        glDeleteTextures(1, &target.resolvedColorTexture);
        target.resolvedColorTexture = 0;
    }
}

static GLuint createColorTexture(int width, int height) {
    GLuint texture = 0;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    return texture;
}

static GLuint createMsaaRenderbuffer(
    GLenum internalFormat,
    int width,
    int height,
    int samples
) {
    GLuint renderbuffer = 0;

    glGenRenderbuffers(1, &renderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);

    glRenderbufferStorageMultisample(
        GL_RENDERBUFFER,
        samples,
        internalFormat,
        width,
        height
    );

    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    return renderbuffer;
}

static void checkFramebuffer(const char* name) {
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(
            stderr,
            "%s incomplete: 0x%04X\n",
            name,
            status
        );

        std::exit(EXIT_FAILURE);
    }
}

static void resizeTarget(
    MrtMsaaTarget& target,
    int width,
    int height
) {
    if (
        width <= 0 ||
        height <= 0 ||
        (
            target.width == width &&
            target.height == height
            )
        ) {
        return;
    }

    if (!target.msaaFramebuffer) {
        glGenFramebuffers(1, &target.msaaFramebuffer);
    }

    if (!target.resolveFramebuffer) {
        glGenFramebuffers(1, &target.resolveFramebuffer);
    }

    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);

    target.samples = std::max(1, std::min(4, maxSamples));

    deleteTargetAttachments(target);

    target.width = width;
    target.height = height;

    // --------------------------------------------------------
    // MSAA FBO：保存多采样渲染结果
    // --------------------------------------------------------

    target.msaaColorRenderbuffer =
        createMsaaRenderbuffer(
            GL_RGBA8,
            width,
            height,
            target.samples
        );

    target.msaaNormalRenderbuffer =
        createMsaaRenderbuffer(
            GL_RGBA8,
            width,
            height,
            target.samples
        );

    target.msaaDepthRenderbuffer =
        createMsaaRenderbuffer(
            GL_DEPTH_COMPONENT24,
            width,
            height,
            target.samples
        );

    glBindFramebuffer(GL_FRAMEBUFFER, target.msaaFramebuffer);

    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_RENDERBUFFER,
        target.msaaColorRenderbuffer
    );

    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT1,
        GL_RENDERBUFFER,
        target.msaaNormalRenderbuffer
    );

    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER,
        target.msaaDepthRenderbuffer
    );

    const GLenum mrtBuffers[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1
    };

    glDrawBuffers(2, mrtBuffers);

    checkFramebuffer("MSAA framebuffer");

    // --------------------------------------------------------
    // Resolve FBO：保存普通 Texture，后续可以采样
    // --------------------------------------------------------

    target.resolvedColorTexture =
        createColorTexture(width, height);

    target.resolvedNormalTexture =
        createColorTexture(width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, target.resolveFramebuffer);

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        target.resolvedColorTexture,
        0
    );

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT1,
        GL_TEXTURE_2D,
        target.resolvedNormalTexture,
        0
    );

    glDrawBuffers(2, mrtBuffers);

    checkFramebuffer("Resolve framebuffer");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    std::printf(
        "MRT MSAA target resized: %d x %d, samples = %d\n",
        width,
        height,
        target.samples
    );
}

static Renderer createRenderer() {
    Renderer renderer;

    renderer.sceneProgram = createSceneProgram();
    renderer.screenProgram = createScreenProgram();

    renderer.cube = createCubeMesh();
    renderer.screenQuad = createScreenQuad();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    return renderer;
}

static void renderSceneToMsaaTarget(
    const Renderer& renderer,
    const MrtMsaaTarget& target,
    float elapsedSeconds
) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.msaaFramebuffer);

    const GLenum mrtBuffers[] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1
    };

    // 这一句非常关键：
    // 它告诉 GL 当前 Fragment Shader 的多个输出分别写入哪些 Attachment。
    glDrawBuffers(2, mrtBuffers);

    glViewport(0, 0, target.width, target.height);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT
    );

    glm::vec3 cameraPosition(0.0f, 1.2f, 4.5f);

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
        glm::mat4 model(1.0f);

        model = glm::translate(
            model,
            glm::vec3((i - 1) * 1.25f, 0.0f, 0.0f)
        );

        model = glm::rotate(
            model,
            elapsedSeconds * (0.7f + i * 0.25f),
            glm::normalize(glm::vec3(1.0f, 0.6f + i, 0.3f))
        );

        glm::mat3 normalMatrix =
            glm::transpose(glm::inverse(glm::mat3(model)));

        glm::vec3 color(
            i == 0 ? 0.95f : 0.25f,
            i == 1 ? 0.85f : 0.35f,
            i == 2 ? 1.00f : 0.45f
        );

        glUniformMatrix4fv(
            renderer.sceneProgram.modelLocation,
            1,
            GL_FALSE,
            glm::value_ptr(model)
        );

        glUniformMatrix3fv(
            renderer.sceneProgram.normalMatrixLocation,
            1,
            GL_FALSE,
            glm::value_ptr(normalMatrix)
        );

        glUniform3fv(
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

static void resolveMsaaTarget(const MrtMsaaTarget& target) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target.msaaFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.resolveFramebuffer);

    // --------------------------------------------------------
    // Resolve COLOR_ATTACHMENT0 -> resolvedColorTexture
    // --------------------------------------------------------

    glReadBuffer(GL_COLOR_ATTACHMENT0);

    {
        // 第 0 个 draw buffer 对应 GL_COLOR_ATTACHMENT0。
        // 第 1 个位置不写，所以设为 GL_NONE。
        const GLenum drawBuffers[] = {
            GL_COLOR_ATTACHMENT0,
            GL_NONE
        };

        glDrawBuffers(2, drawBuffers);
    }

    glBlitFramebuffer(
        0,
        0,
        target.width,
        target.height,
        0,
        0,
        target.width,
        target.height,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST
    );

    // --------------------------------------------------------
    // Resolve COLOR_ATTACHMENT1 -> resolvedNormalTexture
    // --------------------------------------------------------

    glReadBuffer(GL_COLOR_ATTACHMENT1);

    {
        // GLES 要求第 i 个值匹配 GL_COLOR_ATTACHMENTi 或 GL_NONE。
        // 所以如果要写 GL_COLOR_ATTACHMENT1，必须把它放在数组第 1 项。
        const GLenum drawBuffers[] = {
            GL_NONE,
            GL_COLOR_ATTACHMENT1
        };

        glDrawBuffers(2, drawBuffers);
    }

    glBlitFramebuffer(
        0,
        0,
        target.width,
        target.height,
        0,
        0,
        target.width,
        target.height,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST
    );

    // 恢复 Resolve FBO 的 MRT 写入状态，方便后续调试或再次使用。
    {
        const GLenum drawBuffers[] = {
            GL_COLOR_ATTACHMENT0,
            GL_COLOR_ATTACHMENT1
        };

        glDrawBuffers(2, drawBuffers);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

static void drawScreenTexture(
    const Renderer& renderer,
    GLuint texture,
    float offsetX,
    float scaleX
) {
    glUseProgram(renderer.screenProgram.program);

    glUniform2f(
        renderer.screenProgram.offsetLocation,
        offsetX,
        0.0f
    );

    glUniform2f(
        renderer.screenProgram.scaleLocation,
        scaleX,
        0.88f
    );

    glBindVertexArray(renderer.screenQuad.vao);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glDrawElements(
        GL_TRIANGLES,
        renderer.screenQuad.indexCount,
        GL_UNSIGNED_SHORT,
        nullptr
    );
}

static void renderResolvedTexturesToScreen(
    const Renderer& renderer,
    const MrtMsaaTarget& target,
    int width,
    int height
) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(0, 0, width, height);

    glDisable(GL_DEPTH_TEST);

    glClearColor(0.02f, 0.025f, 0.035f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 左边显示最终颜色。
    drawScreenTexture(
        renderer,
        target.resolvedColorTexture,
        -0.52f,
        0.46f
    );

    // 右边显示法线图。
    drawScreenTexture(
        renderer,
        target.resolvedNormalTexture,
        0.52f,
        0.46f
    );

    glBindTexture(GL_TEXTURE_2D, 0);
}

static void renderFrame(
    Renderer& renderer,
    int width,
    int height,
    float elapsedSeconds
) {
    resizeTarget(renderer.target, width, height);

    renderSceneToMsaaTarget(
        renderer,
        renderer.target,
        elapsedSeconds
    );

    resolveMsaaTarget(renderer.target);

    renderResolvedTexturesToScreen(
        renderer,
        renderer.target,
        width,
        height
    );
}

static void destroyMesh(Mesh& mesh) {
    glDeleteBuffers(1, &mesh.ebo);
    glDeleteBuffers(1, &mesh.vbo);
    glDeleteVertexArrays(1, &mesh.vao);
}

static void destroyTarget(MrtMsaaTarget& target) {
    deleteTargetAttachments(target);

    if (target.resolveFramebuffer) {
        glDeleteFramebuffers(1, &target.resolveFramebuffer);
    }

    if (target.msaaFramebuffer) {
        glDeleteFramebuffers(1, &target.msaaFramebuffer);
    }
}

static void destroyRenderer(Renderer& renderer) {
    destroyTarget(renderer.target);
    destroyMesh(renderer.screenQuad);
    destroyMesh(renderer.cube);

    glDeleteProgram(renderer.screenProgram.program);
    glDeleteProgram(renderer.sceneProgram.program);
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
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

    GLFWwindow* window = glfwCreateWindow(
        1100,
        720,
        "19 - MRT and MSAA",
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

    GLint maxDrawBuffers = 0;
    GLint maxColorAttachments = 0;
    GLint maxSamples = 0;

    glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxColorAttachments);
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);

    std::printf("GL_MAX_DRAW_BUFFERS      : %d\n", maxDrawBuffers);
    std::printf("GL_MAX_COLOR_ATTACHMENTS : %d\n", maxColorAttachments);
    std::printf("GL_MAX_SAMPLES           : %d\n", maxSamples);
    std::printf("Left: color attachment, Right: normal attachment\n");

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