#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// GPU 资源：创建后长期存在，退出前释放。
struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
};

struct Renderer {
    GLuint program = 0;
    Mesh cube;

    GLint modelLocation = -1;
    GLint viewLocation = -1;
    GLint projectionLocation = -1;
    GLint tintLocation = -1;
};

// CPU 侧场景数据：每帧可以修改。
struct Entity {
    glm::vec3 position;
    glm::vec3 rotationAxis;
    glm::vec3 tint;
    float rotationSpeed;
};

struct Scene {
    std::array<Entity, 3> entities;
    glm::vec3 cameraPosition;
};

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
        uProjection * uView * uModel * vec4(aPosition, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

uniform vec3 uTint;
out vec4 fragColor;

void main() {
    fragColor = vec4(uTint, 1.0);
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

// 只在初始化阶段调用：将静态模型上传到 GPU。
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
        0, 1, 2,  2, 3, 0, // 后面
        4, 6, 5,  6, 4, 7, // 前面
        0, 4, 5,  5, 1, 0, // 底面
        3, 2, 6,  6, 7, 3, // 顶面
        0, 3, 7,  7, 4, 0, // 左面
        1, 5, 6,  6, 2, 1  // 右面
    };

    Mesh mesh;
    mesh.indexCount =
        static_cast<GLsizei>(std::size(indices));

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

// Renderer 初始化：创建 Shader、上传 Mesh、缓存 Uniform Location。
static Renderer createRenderer() {
    Renderer renderer;
    renderer.program = createProgram();
    renderer.cube = createCubeMesh();

    renderer.modelLocation =
        glGetUniformLocation(renderer.program, "uModel");

    renderer.viewLocation =
        glGetUniformLocation(renderer.program, "uView");

    renderer.projectionLocation =
        glGetUniformLocation(renderer.program, "uProjection");

    renderer.tintLocation =
        glGetUniformLocation(renderer.program, "uTint");

    glEnable(GL_DEPTH_TEST);

    return renderer;
}

// Scene 初始化：这里只创建 CPU 侧的业务数据。
static Scene createScene() {
    Scene scene;

    scene.cameraPosition = glm::vec3(0.0f, 1.2f, 5.0f);

    scene.entities = { {
        {
            glm::vec3(-1.5f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.95f, 0.25f, 0.20f),
            0.9f
        },
        {
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(1.0f, 1.0f, 0.0f),
            glm::vec3(0.20f, 0.80f, 0.45f),
            1.2f
        },
        {
            glm::vec3(1.5f, 0.0f, 0.0f),
            glm::vec3(1.0f, 0.0f, 1.0f),
            glm::vec3(0.25f, 0.45f, 1.00f),
            1.5f
        }
    } };

    return scene;
}

// CPU 更新阶段：真实项目通常在这里处理输入、动画和业务逻辑。
static void updateScene(Scene& scene, float elapsedSeconds) {
    // 这里暂时不修改 Scene。
    // 每个物体的旋转角度会在渲染时根据时间计算。
    //
    // 后续可以尝试：
    // scene.cameraPosition.x = sin(elapsedSeconds) * 5.0f;
    (void)scene;
    (void)elapsedSeconds;
}

// GPU 渲染阶段：读取 Scene，不负责修改业务状态。
static void renderScene(
    const Renderer& renderer,
    const Scene& scene,
    int width,
    int height,
    float elapsedSeconds
) {
    glViewport(0, 0, width, height);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view = glm::lookAt(
        scene.cameraPosition,
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    glm::mat4 projection = glm::perspective(
        glm::radians(60.0f),
        static_cast<float>(width) / static_cast<float>(height),
        0.1f,
        100.0f
    );

    // Program 和 VAO 对所有立方体相同，每帧只需要绑定一次。
    glUseProgram(renderer.program);
    glBindVertexArray(renderer.cube.vao);

    // Frame Uniform：本帧所有物体共享。
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

    for (const Entity& entity : scene.entities) {
        glm::mat4 model(1.0f);

        model = glm::translate(model, entity.position);

        model = glm::rotate(
            model,
            elapsedSeconds * entity.rotationSpeed,
            glm::normalize(entity.rotationAxis)
        );

        // Object Uniform：每个物体各不相同。
        glUniformMatrix4fv(
            renderer.modelLocation,
            1,
            GL_FALSE,
            glm::value_ptr(model)
        );

        glUniform3fv(
            renderer.tintLocation,
            1,
            glm::value_ptr(entity.tint)
        );

        // 同一个 Mesh 被重复绘制三次，但使用不同的 Model 和 Tint。
        glDrawElements(
            GL_TRIANGLES,
            renderer.cube.indexCount,
            GL_UNSIGNED_SHORT,
            nullptr
        );
    }
}

static void destroyRenderer(Renderer& renderer) {
    glDeleteBuffers(1, &renderer.cube.ebo);
    glDeleteBuffers(1, &renderer.cube.vbo);
    glDeleteVertexArrays(1, &renderer.cube.vao);
    glDeleteProgram(renderer.program);
}

int main() {
    if (!glfwInit()) {
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(
        1000,
        700,
        "12 - Render Loop Architecture",
        nullptr,
        nullptr
    );

    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    Renderer renderer = createRenderer();
    Scene scene = createScene();

    while (!glfwWindowShouldClose(window)) {
        // 1. 输入阶段
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // 2. CPU 更新阶段
        float elapsedSeconds =
            static_cast<float>(glfwGetTime());

        updateScene(scene, elapsedSeconds);

        // 3. GPU 渲染阶段
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        if (width > 0 && height > 0) {
            renderScene(
                renderer,
                scene,
                width,
                height,
                elapsedSeconds
            );

            // 4. Present 阶段
            glfwSwapBuffers(window);
        }
    }

    // 必须在 Context 仍然有效时释放 GLES 资源。
    destroyRenderer(renderer);

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}