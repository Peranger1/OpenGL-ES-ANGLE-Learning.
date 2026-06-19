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
#include <vector>

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
};

struct SceneObject {
    glm::vec3 position;
    glm::vec3 color;

    // 双缓冲 query，避免本帧刚发起就读取结果。
    GLuint queries[2]{};

    bool visible = true;
    bool hasResult = false;
};

struct Renderer {
    GLuint program = 0;

    GLint modelLocation = -1;
    GLint viewLocation = -1;
    GLint projectionLocation = -1;
    GLint colorLocation = -1;

    Mesh cube;
    Mesh wall;

    std::vector<SceneObject> objects;

    int frameIndex = 0;
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

static Mesh createMesh(
    const float* vertices,
    GLsizeiptr vertexBytes,
    const std::uint16_t* indices,
    GLsizeiptr indexBytes,
    GLsizei indexCount
) {
    Mesh mesh;
    mesh.indexCount = indexCount;

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertexBytes, vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexBytes, indices, GL_STATIC_DRAW);

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

    return createMesh(
        vertices,
        sizeof(vertices),
        indices,
        sizeof(indices),
        36
    );
}

static Mesh createWallMesh() {
    // 一个位于 XY 平面的矩形，后续通过 model matrix 放到场景中。
    const float vertices[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f
    };

    const std::uint16_t indices[] = {
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

static std::vector<SceneObject> createObjects() {
    std::vector<SceneObject> objects = {
        {
            glm::vec3(-0.75f, -0.25f, -0.55f),
            glm::vec3(1.00f, 0.25f, 0.20f)
        },
        {
            glm::vec3(0.75f, -0.20f, -0.75f),
            glm::vec3(0.20f, 0.85f, 0.35f)
        },
        {
            glm::vec3(-2.10f, -0.25f, -0.35f),
            glm::vec3(0.25f, 0.55f, 1.00f)
        },
        {
            glm::vec3(2.10f, -0.25f, -0.35f),
            glm::vec3(1.00f, 0.80f, 0.20f)
        },
        {
            glm::vec3(0.0f, -0.15f, 1.20f),
            glm::vec3(0.90f, 0.35f, 1.00f)
        }
    };

    for (SceneObject& object : objects) {
        glGenQueries(2, object.queries);
    }

    return objects;
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

    renderer.cube = createCubeMesh();
    renderer.wall = createWallMesh();
    renderer.objects = createObjects();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    return renderer;
}

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

static glm::mat4 makeCubeModel(
    const SceneObject& object,
    float elapsedSeconds
) {
    glm::mat4 model(1.0f);

    model = glm::translate(model, object.position);

    model = glm::rotate(
        model,
        elapsedSeconds * 0.6f + object.position.x,
        glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f))
    );

    model = glm::scale(
        model,
        glm::vec3(0.58f)
    );

    return model;
}

static glm::mat4 makeWallModel() {
    glm::mat4 model(1.0f);

    // 摄像机在 +Z，看向 -Z。
    // 墙位于中间，遮挡后方中央两个 cube。
    model = glm::translate(
        model,
        glm::vec3(0.0f, 0.0f, 0.25f)
    );

    model = glm::scale(
        model,
        glm::vec3(1.45f, 1.25f, 1.0f)
    );

    return model;
}

static void readPreviousQueryResults(Renderer& renderer) {
    if (renderer.frameIndex < 2) {
        return;
    }

    int previousIndex =
        (renderer.frameIndex + 1) % 2;

    for (SceneObject& object : renderer.objects) {
        GLuint query = object.queries[previousIndex];

        GLuint available = GL_FALSE;

        glGetQueryObjectuiv(
            query,
            GL_QUERY_RESULT_AVAILABLE,
            &available
        );

        if (available == GL_TRUE) {
            GLuint result = GL_FALSE;

            glGetQueryObjectuiv(
                query,
                GL_QUERY_RESULT,
                &result
            );

            object.visible = result == GL_TRUE;
            object.hasResult = true;
        }

        // 如果还没 available，就沿用上一帧结果。
        // 这样避免 CPU 阻塞等待 GPU。
    }
}

static void issueVisibilityQueries(
    Renderer& renderer,
    float elapsedSeconds
) {
    int queryIndex =
        renderer.frameIndex % 2;

    // Query pass 只想知道“有没有样本通过深度测试”。
    // 不需要写颜色，也不应该改深度。
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDepthMask(GL_FALSE);

    // 仍然保留深度测试。
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    for (SceneObject& object : renderer.objects) {
        GLuint query = object.queries[queryIndex];

        glBeginQuery(
            GL_ANY_SAMPLES_PASSED,
            query
        );

        drawMesh(
            renderer,
            renderer.cube,
            makeCubeModel(object, elapsedSeconds),
            glm::vec4(1.0f)
        );

        glEndQuery(GL_ANY_SAMPLES_PASSED);
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
}

static void renderVisibleObjects(
    const Renderer& renderer,
    float elapsedSeconds
) {
    for (const SceneObject& object : renderer.objects) {
        if (!object.visible) {
            continue;
        }

        drawMesh(
            renderer,
            renderer.cube,
            makeCubeModel(object, elapsedSeconds),
            glm::vec4(object.color, 1.0f)
        );
    }
}

static void printVisibilityEverySecond(Renderer& renderer) {
    if (renderer.frameIndex % 60 != 0) {
        return;
    }

    std::printf("Visible objects: ");

    for (std::size_t i = 0; i < renderer.objects.size(); ++i) {
        const SceneObject& object = renderer.objects[i];

        std::printf(
            "%zu=%s%s",
            i,
            object.visible ? "Y" : "N",
            i + 1 == renderer.objects.size() ? "" : ", "
        );
    }

    std::printf("\n");
}

static void renderFrame(
    Renderer& renderer,
    int width,
    int height,
    float elapsedSeconds
) {
    readPreviousQueryResults(renderer);

    glViewport(0, 0, width, height);

    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT
    );

    glm::vec3 cameraPosition(0.0f, 0.35f, 4.8f);

    glm::mat4 view = glm::lookAt(
        cameraPosition,
        glm::vec3(0.0f, -0.05f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    glm::mat4 projection = glm::perspective(
        glm::radians(55.0f),
        static_cast<float>(width) /
        static_cast<float>(height),
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

    // 1. 先绘制遮挡物，让深度缓冲区记录墙的位置。
    drawMesh(
        renderer,
        renderer.wall,
        makeWallModel(),
        glm::vec4(0.28f, 0.30f, 0.34f, 1.0f)
    );

    // 2. 发起可见性查询。
    // 这一步只读深度，不写颜色和深度。
    issueVisibilityQueries(
        renderer,
        elapsedSeconds
    );

    // 3. 根据上一帧 query 结果绘制真实物体。
    renderVisibleObjects(
        renderer,
        elapsedSeconds
    );

    printVisibilityEverySecond(renderer);

    ++renderer.frameIndex;
}

static void destroyMesh(Mesh& mesh) {
    glDeleteBuffers(1, &mesh.ebo);
    glDeleteBuffers(1, &mesh.vbo);
    glDeleteVertexArrays(1, &mesh.vao);
}

static void destroyRenderer(Renderer& renderer) {
    for (SceneObject& object : renderer.objects) {
        glDeleteQueries(2, object.queries);
    }

    destroyMesh(renderer.wall);
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
        "30 - Occlusion Query",
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
    std::printf("The gray wall occludes some cubes. Query results are read one frame later.\n");

    Renderer renderer = createRenderer();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

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