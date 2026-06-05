#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================
// 1. UBO Binding Point 约定
// ============================================================
//
// Binding Point 是 Program 和 Buffer 之间的连接点。
// 多个 Program 可以把同名或不同名 Uniform Block 绑定到同一个 Binding Point。

constexpr GLuint CAMERA_BINDING_POINT = 0;
constexpr GLuint OBJECT_BINDING_POINT = 1;

// std140 下：
// mat4 占 64 字节
// vec4 占 16 字节
//
// 为了避免 C++ struct 对齐和 GLSL std140 不一致，
// 这里直接使用 float 数组手动组织内存。

struct CameraBlockData {
    float view[16];
    float projection[16];
    float cameraPosition[4];
};

struct ObjectBlockData {
    float model[16];
    float color[4];
};

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
};

struct Program {
    GLuint id = 0;
};

struct Renderer {
    Program litProgram;
    Program normalProgram;

    Mesh cube;

    GLuint cameraBuffer = 0;
    GLuint objectBuffer = 0;
};

// ============================================================
// 2. 工具函数
// ============================================================

static void copyMat4(float* destination, const glm::mat4& matrix) {
    std::memcpy(
        destination,
        glm::value_ptr(matrix),
        sizeof(float) * 16
    );
}

static void copyVec4(float* destination, const glm::vec4& value) {
    std::memcpy(
        destination,
        glm::value_ptr(value),
        sizeof(float) * 4
    );
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

static void bindUniformBlock(
    GLuint program,
    const char* blockName,
    GLuint bindingPoint
) {
    GLuint blockIndex =
        glGetUniformBlockIndex(program, blockName);

    if (blockIndex == GL_INVALID_INDEX) {
        std::fprintf(
            stderr,
            "Uniform block not found: %s\n",
            blockName
        );

        std::exit(EXIT_FAILURE);
    }

    // 将 Program 中的 Uniform Block 绑定到指定 Binding Point。
    glUniformBlockBinding(
        program,
        blockIndex,
        bindingPoint
    );
}

// ============================================================
// 3. Shader Programs
// ============================================================

static Program createLitProgram() {
    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

// CameraBlock 每帧更新一次。
// 两个 Program 都会读取这一份数据。
layout(std140) uniform CameraBlock {
    mat4 uView;
    mat4 uProjection;
    vec4 uCameraPosition;
};

// ObjectBlock 每个物体更新一次。
layout(std140) uniform ObjectBlock {
    mat4 uModel;
    vec4 uColor;
};

out vec3 vWorldPosition;
out vec3 vWorldNormal;

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);

    vWorldPosition = worldPosition.xyz;

    // 本实验没有非等比缩放，所以可以用 mat3(uModel) 变换法线。
    vWorldNormal = normalize(mat3(uModel) * aNormal);

    gl_Position = uProjection * uView * worldPosition;
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

layout(std140) uniform CameraBlock {
    mat4 uView;
    mat4 uProjection;
    vec4 uCameraPosition;
};

layout(std140) uniform ObjectBlock {
    mat4 uModel;
    vec4 uColor;
};

in vec3 vWorldPosition;
in vec3 vWorldNormal;

out vec4 fragColor;

void main() {
    vec3 normal = normalize(vWorldNormal);

    vec3 lightDirection =
        normalize(vec3(-0.5, 0.8, 0.6));

    float diffuse =
        max(dot(normal, lightDirection), 0.0);

    vec3 viewDirection =
        normalize(uCameraPosition.xyz - vWorldPosition);

    vec3 reflectionDirection =
        reflect(-lightDirection, normal);

    float specular = pow(
        max(dot(viewDirection, reflectionDirection), 0.0),
        32.0
    );

    vec3 color =
        uColor.rgb * (0.20 + diffuse * 0.80) +
        vec3(1.0) * specular * 0.35;

    fragColor = vec4(color, 1.0);
}
)";

    Program program;
    program.id = linkProgram(vertexSource, fragmentSource);

    bindUniformBlock(
        program.id,
        "CameraBlock",
        CAMERA_BINDING_POINT
    );

    bindUniformBlock(
        program.id,
        "ObjectBlock",
        OBJECT_BINDING_POINT
    );

    return program;
}

static Program createNormalProgram() {
    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

layout(std140) uniform CameraBlock {
    mat4 uView;
    mat4 uProjection;
    vec4 uCameraPosition;
};

layout(std140) uniform ObjectBlock {
    mat4 uModel;
    vec4 uColor;
};

out vec3 vWorldNormal;

void main() {
    vWorldNormal = normalize(mat3(uModel) * aNormal);
    gl_Position =
        uProjection *
        uView *
        uModel *
        vec4(aPosition, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

layout(std140) uniform ObjectBlock {
    mat4 uModel;
    vec4 uColor;
};

in vec3 vWorldNormal;

out vec4 fragColor;

void main() {
    vec3 normal = normalize(vWorldNormal);

    // 将 -1..1 的法线方向映射到 0..1，方便作为颜色观察。
    vec3 normalColor = normal * 0.5 + 0.5;

    // 这里稍微混入 uColor，确保 ObjectBlock 中的 color 成员也被使用。
    fragColor = vec4(normalColor * 0.85 + uColor.rgb * 0.15, 1.0);
}
)";

    Program program;
    program.id = linkProgram(vertexSource, fragmentSource);

    bindUniformBlock(
        program.id,
        "CameraBlock",
        CAMERA_BINDING_POINT
    );

    bindUniformBlock(
        program.id,
        "ObjectBlock",
        OBJECT_BINDING_POINT
    );

    return program;
}

// ============================================================
// 4. Cube Mesh
// ============================================================

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

// ============================================================
// 5. UBO 创建与更新
// ============================================================

static GLuint createUniformBuffer(
    GLsizeiptr size,
    GLuint bindingPoint
) {
    GLuint buffer = 0;

    glGenBuffers(1, &buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer);

    glBufferData(
        GL_UNIFORM_BUFFER,
        size,
        nullptr,
        GL_DYNAMIC_DRAW
    );

    // 将 Buffer 绑定到 Binding Point。
    // Program 中的 Uniform Block 会通过这个 Binding Point 读取数据。
    glBindBufferBase(
        GL_UNIFORM_BUFFER,
        bindingPoint,
        buffer
    );

    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    return buffer;
}

static void updateCameraBuffer(
    GLuint cameraBuffer,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPosition
) {
    CameraBlockData data{};

    copyMat4(data.view, view);
    copyMat4(data.projection, projection);

    data.cameraPosition[0] = cameraPosition.x;
    data.cameraPosition[1] = cameraPosition.y;
    data.cameraPosition[2] = cameraPosition.z;
    data.cameraPosition[3] = 1.0f;

    glBindBuffer(GL_UNIFORM_BUFFER, cameraBuffer);

    glBufferSubData(
        GL_UNIFORM_BUFFER,
        0,
        sizeof(data),
        &data
    );

    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

static void updateObjectBuffer(
    GLuint objectBuffer,
    const glm::mat4& model,
    const glm::vec4& color
) {
    ObjectBlockData data{};

    copyMat4(data.model, model);
    copyVec4(data.color, color);

    glBindBuffer(GL_UNIFORM_BUFFER, objectBuffer);

    glBufferSubData(
        GL_UNIFORM_BUFFER,
        0,
        sizeof(data),
        &data
    );

    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

// ============================================================
// 6. Renderer
// ============================================================

static Renderer createRenderer() {
    Renderer renderer;

    renderer.litProgram = createLitProgram();
    renderer.normalProgram = createNormalProgram();
    renderer.cube = createCubeMesh();

    renderer.cameraBuffer =
        createUniformBuffer(
            sizeof(CameraBlockData),
            CAMERA_BINDING_POINT
        );

    renderer.objectBuffer =
        createUniformBuffer(
            sizeof(ObjectBlockData),
            OBJECT_BINDING_POINT
        );

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    return renderer;
}

static void drawCube(
    const Renderer& renderer,
    GLuint program,
    const glm::mat4& model,
    const glm::vec4& color
) {
    // Object UBO 每个物体更新一次。
    updateObjectBuffer(
        renderer.objectBuffer,
        model,
        color
    );

    glUseProgram(program);
    glBindVertexArray(renderer.cube.vao);

    glDrawElements(
        GL_TRIANGLES,
        renderer.cube.indexCount,
        GL_UNSIGNED_SHORT,
        nullptr
    );
}

static void renderFrame(
    Renderer& renderer,
    int width,
    int height,
    float elapsedSeconds
) {
    glViewport(0, 0, width, height);

    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT
    );

    glm::vec3 cameraPosition(
        0.0f,
        1.1f,
        4.5f
    );

    glm::mat4 view = glm::lookAt(
        cameraPosition,
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    glm::mat4 projection = glm::perspective(
        glm::radians(60.0f),
        static_cast<float>(width) /
        static_cast<float>(height),
        0.1f,
        100.0f
    );

    // Camera UBO 每帧只更新一次。
    // 两个 Program 都会读取这份数据。
    updateCameraBuffer(
        renderer.cameraBuffer,
        view,
        projection,
        cameraPosition
    );

    glm::mat4 leftModel(1.0f);

    leftModel = glm::translate(
        leftModel,
        glm::vec3(-0.9f, 0.0f, 0.0f)
    );

    leftModel = glm::rotate(
        leftModel,
        elapsedSeconds * 0.8f,
        glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f))
    );

    drawCube(
        renderer,
        renderer.litProgram.id,
        leftModel,
        glm::vec4(0.20f, 0.70f, 1.00f, 1.0f)
    );

    glm::mat4 rightModel(1.0f);

    rightModel = glm::translate(
        rightModel,
        glm::vec3(0.9f, 0.0f, 0.0f)
    );

    rightModel = glm::rotate(
        rightModel,
        elapsedSeconds * 0.8f,
        glm::normalize(glm::vec3(1.0f, 0.5f, 0.1f))
    );

    drawCube(
        renderer,
        renderer.normalProgram.id,
        rightModel,
        glm::vec4(1.00f, 0.35f, 0.20f, 1.0f)
    );
}

// ============================================================
// 7. 清理
// ============================================================

static void destroyMesh(Mesh& mesh) {
    glDeleteBuffers(1, &mesh.ebo);
    glDeleteBuffers(1, &mesh.vbo);
    glDeleteVertexArrays(1, &mesh.vao);
}

static void destroyRenderer(Renderer& renderer) {
    glDeleteBuffers(1, &renderer.objectBuffer);
    glDeleteBuffers(1, &renderer.cameraBuffer);

    destroyMesh(renderer.cube);

    glDeleteProgram(renderer.normalProgram.id);
    glDeleteProgram(renderer.litProgram.id);
}

// ============================================================
// 8. 主函数
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
        "20 - Uniform Buffer",
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

    GLint maxUniformBufferBindings = 0;
    GLint maxUniformBlockSize = 0;
    GLint uniformBufferOffsetAlignment = 0;

    glGetIntegerv(
        GL_MAX_UNIFORM_BUFFER_BINDINGS,
        &maxUniformBufferBindings
    );

    glGetIntegerv(
        GL_MAX_UNIFORM_BLOCK_SIZE,
        &maxUniformBlockSize
    );

    glGetIntegerv(
        GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,
        &uniformBufferOffsetAlignment
    );

    std::printf(
        "GL_MAX_UNIFORM_BUFFER_BINDINGS : %d\n",
        maxUniformBufferBindings
    );

    std::printf(
        "GL_MAX_UNIFORM_BLOCK_SIZE      : %d\n",
        maxUniformBlockSize
    );

    std::printf(
        "GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT: %d\n",
        uniformBufferOffsetAlignment
    );

    std::printf(
        "Left cube: lit shader, right cube: normal visualization shader\n"
    );

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