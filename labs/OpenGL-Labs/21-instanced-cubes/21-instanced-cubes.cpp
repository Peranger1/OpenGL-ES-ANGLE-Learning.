#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

struct InstanceData {
    glm::mat4 model;
    glm::vec4 color;
};

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLuint instanceVbo = 0;
    GLsizei indexCount = 0;
};

struct Renderer {
    GLuint program = 0;
    GLint viewLocation = -1;
    GLint projectionLocation = -1;
    Mesh cube;
    std::vector<InstanceData> instances;
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

static GLuint createProgram() {
    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

// mat4 会占用 4 个 attribute location。
// 这里 aInstanceModel 使用 location 2、3、4、5。
layout(location = 2) in mat4 aInstanceModel;

// 每个实例一份颜色。
layout(location = 6) in vec4 aInstanceColor;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldNormal;
out vec4 vColor;

void main() {
    vec4 worldPosition =
        aInstanceModel * vec4(aPosition, 1.0);

    // 本例只使用平移、旋转和统一缩放，所以 mat3(model) 变换法线足够。
    vWorldNormal =
        normalize(mat3(aInstanceModel) * aNormal);

    vColor = aInstanceColor;

    gl_Position =
        uProjection * uView * worldPosition;
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec3 vWorldNormal;
in vec4 vColor;

out vec4 fragColor;

void main() {
    vec3 normal = normalize(vWorldNormal);

    vec3 lightDirection =
        normalize(vec3(-0.4, 0.8, 0.6));

    float diffuse =
        max(dot(normal, lightDirection), 0.0);

    vec3 color =
        vColor.rgb * (0.22 + diffuse * 0.78);

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
    glGenBuffers(1, &mesh.instanceVbo);

    glBindVertexArray(mesh.vao);

    // --------------------------------------------------------
    // Per-vertex buffer
    // --------------------------------------------------------

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

    // --------------------------------------------------------
    // Per-instance buffer
    // --------------------------------------------------------
    //
    // InstanceData:
    //   mat4 model  -> location 2, 3, 4, 5
    //   vec4 color  -> location 6
    //
    // mat4 不能用一个 attribute location 表示。
    // 它会拆成四个 vec4 attribute，每列一个 location。

    glBindBuffer(GL_ARRAY_BUFFER, mesh.instanceVbo);

    for (int column = 0; column < 4; ++column) {
        GLuint location = static_cast<GLuint>(2 + column);

        glVertexAttribPointer(
            location,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof(InstanceData),
            reinterpret_cast<void*>(
                offsetof(InstanceData, model) +
                sizeof(glm::vec4) * column
                )
        );

        glEnableVertexAttribArray(location);

        // divisor = 1 表示：
        // 这个 attribute 不是每个顶点前进一次，
        // 而是每个实例前进一次。
        glVertexAttribDivisor(location, 1);
    }

    glVertexAttribPointer(
        6,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(InstanceData),
        reinterpret_cast<void*>(
            offsetof(InstanceData, color)
            )
    );

    glEnableVertexAttribArray(6);
    glVertexAttribDivisor(6, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return mesh;
}

static std::vector<InstanceData> createInstances() {
    std::vector<InstanceData> instances;

    constexpr int gridX = 18;
    constexpr int gridZ = 18;

    instances.reserve(gridX * gridZ);

    for (int z = 0; z < gridZ; ++z) {
        for (int x = 0; x < gridX; ++x) {
            float fx = static_cast<float>(x) -
                static_cast<float>(gridX - 1) * 0.5f;

            float fz = static_cast<float>(z) -
                static_cast<float>(gridZ - 1) * 0.5f;

            InstanceData instance{};

            instance.model =
                glm::translate(
                    glm::mat4(1.0f),
                    glm::vec3(fx * 1.05f, 0.0f, fz * 1.05f)
                );

            float r = static_cast<float>(x) /
                static_cast<float>(gridX - 1);

            float b = static_cast<float>(z) /
                static_cast<float>(gridZ - 1);

            instance.color =
                glm::vec4(0.25f + r * 0.75f, 0.75f, 0.25f + b * 0.75f, 1.0f);

            instances.push_back(instance);
        }
    }

    return instances;
}

static Renderer createRenderer() {
    Renderer renderer;

    renderer.program = createProgram();
    renderer.viewLocation =
        glGetUniformLocation(renderer.program, "uView");
    renderer.projectionLocation =
        glGetUniformLocation(renderer.program, "uProjection");

    renderer.cube = createCubeMesh();
    renderer.instances = createInstances();

    glBindBuffer(GL_ARRAY_BUFFER, renderer.cube.instanceVbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        renderer.instances.size() * sizeof(InstanceData),
        renderer.instances.data(),
        GL_DYNAMIC_DRAW
    );
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    return renderer;
}

static void updateInstances(
    Renderer& renderer,
    float elapsedSeconds
) {
    constexpr int gridX = 18;
    constexpr int gridZ = 18;

    int index = 0;

    for (int z = 0; z < gridZ; ++z) {
        for (int x = 0; x < gridX; ++x) {
            float fx = static_cast<float>(x) -
                static_cast<float>(gridX - 1) * 0.5f;

            float fz = static_cast<float>(z) -
                static_cast<float>(gridZ - 1) * 0.5f;

            float wave =
                std::sin(elapsedSeconds * 1.6f + fx * 0.45f + fz * 0.35f);

            glm::mat4 model(1.0f);

            model = glm::translate(
                model,
                glm::vec3(
                    fx * 1.05f,
                    wave * 0.35f,
                    fz * 1.05f
                )
            );

            model = glm::rotate(
                model,
                elapsedSeconds * 0.7f + wave,
                glm::normalize(glm::vec3(0.4f, 1.0f, 0.2f))
            );

            model = glm::scale(
                model,
                glm::vec3(0.62f)
            );

            renderer.instances[index].model = model;

            ++index;
        }
    }

    // 每帧更新 Instance Buffer。
    // 真实项目中如果数据量更大，可以进一步使用 buffer orphaning、
    // glMapBufferRange 或环形缓冲区避免同步问题。
    glBindBuffer(GL_ARRAY_BUFFER, renderer.cube.instanceVbo);

    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        renderer.instances.size() * sizeof(InstanceData),
        renderer.instances.data()
    );

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void renderFrame(
    Renderer& renderer,
    int width,
    int height,
    float elapsedSeconds
) {
    updateInstances(renderer, elapsedSeconds);

    glViewport(0, 0, width, height);

    glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::vec3 cameraPosition(
        std::sin(elapsedSeconds * 0.25f) * 7.0f,
        7.0f,
        14.0f
    );

    glm::mat4 view = glm::lookAt(
        cameraPosition,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    glm::mat4 projection = glm::perspective(
        glm::radians(55.0f),
        static_cast<float>(width) / static_cast<float>(height),
        0.1f,
        100.0f
    );

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

    glBindVertexArray(renderer.cube.vao);

    // 关键调用：
    // indexCount 决定一个立方体怎么画。
    // instanceCount 决定这个立方体要画多少份。
    glDrawElementsInstanced(
        GL_TRIANGLES,
        renderer.cube.indexCount,
        GL_UNSIGNED_SHORT,
        nullptr,
        static_cast<GLsizei>(renderer.instances.size())
    );
}

static void destroyMesh(Mesh& mesh) {
    glDeleteBuffers(1, &mesh.instanceVbo);
    glDeleteBuffers(1, &mesh.ebo);
    glDeleteBuffers(1, &mesh.vbo);
    glDeleteVertexArrays(1, &mesh.vao);
}

static void destroyRenderer(Renderer& renderer) {
    destroyMesh(renderer.cube);
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
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

    GLFWwindow* window = glfwCreateWindow(
        1100,
        760,
        "21 - Instanced Cubes",
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

    GLint maxVertexAttribs = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxVertexAttribs);
    std::printf("GL_MAX_VERTEX_ATTRIBS: %d\n", maxVertexAttribs);

    std::printf("One draw call renders 324 cubes.\n");

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