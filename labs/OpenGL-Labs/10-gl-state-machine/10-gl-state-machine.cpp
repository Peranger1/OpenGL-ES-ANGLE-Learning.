#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <cstdio>
#include <cstdlib>

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
};

static bool gReusePreviousState = false;

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

static GLuint createProgram(
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

static Mesh createMesh(const float* vertices, size_t byteCount) {
    Mesh mesh;

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);

    // 后续配置会写入当前绑定的 VAO。
    glBindVertexArray(mesh.vao);

    // 后续 glBufferData 操作当前绑定到 GL_ARRAY_BUFFER 的 VBO。
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, byteCount, vertices, GL_STATIC_DRAW);

    // 告诉 VAO：location = 0 的位置属性如何从 VBO 中读取。
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        reinterpret_cast<void*>(0)
    );
    glEnableVertexAttribArray(0);

    // 告诉 VAO：location = 1 的颜色属性如何从 VBO 中读取。
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        reinterpret_cast<void*>(2 * sizeof(float))
    );
    glEnableVertexAttribArray(1);

    // VAO 已经记录了属性布局以及对应的 VBO。
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return mesh;
}

static void printCurrentState(const char* label) {
    GLint program = 0;
    GLint vao = 0;
    GLint arrayBuffer = 0;

    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);

    std::printf(
        "%s: program=%d, vao=%d, arrayBuffer=%d\n",
        label,
        program,
        vao,
        arrayBuffer
    );
}

static void keyCallback(
    GLFWwindow* window,
    int key,
    int scanCode,
    int action,
    int modifiers
) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
        gReusePreviousState = !gReusePreviousState;

        std::printf(
            "\nMode: %s\n",
            gReusePreviousState
            ? "reuse previous state"
            : "explicitly bind state"
        );
    }
}

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE;
    }

    // GLFW 仍然使用 EGL 创建 OpenGL ES 3 Context。
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(
        900,
        600,
        "10 - GLES State Machine",
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
    std::printf("Press SPACE to switch state binding mode.\n");

    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec3 aColor;

uniform vec2 uOffset;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = vec4(aPosition + uOffset, 0.0, 1.0);
}
)";

    const char* normalFragmentSource = R"(#version 300 es
precision mediump float;

in vec3 vColor;
out vec4 fragColor;

void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

    const char* swappedFragmentSource = R"(#version 300 es
precision mediump float;

in vec3 vColor;
out vec4 fragColor;

void main() {
    fragColor = vec4(vColor.bgr, 1.0);
}
)";

    // 两个 Program 使用相同 Vertex Shader，但 Fragment Shader 不同。
    GLuint normalProgram =
        createProgram(vertexSource, normalFragmentSource);

    GLuint swappedProgram =
        createProgram(vertexSource, swappedFragmentSource);

    GLint normalOffset =
        glGetUniformLocation(normalProgram, "uOffset");

    GLint swappedOffset =
        glGetUniformLocation(swappedProgram, "uOffset");

    const float triangleA[] = {
        // x,     y,      r,    g,    b
        -0.25f, -0.35f,   1.0f, 0.2f, 0.2f,
         0.25f, -0.35f,   0.2f, 1.0f, 0.2f,
         0.00f,  0.35f,   0.2f, 0.4f, 1.0f
    };

    const float triangleB[] = {
        // 第二个 Mesh 形状不同，便于辨认 VAO 是否真正切换。
        -0.32f,  0.28f,   1.0f, 0.7f, 0.1f,
         0.32f,  0.28f,   0.1f, 0.8f, 1.0f,
         0.00f, -0.28f,   0.8f, 0.2f, 1.0f
    };

    Mesh meshA = createMesh(triangleA, sizeof(triangleA));
    Mesh meshB = createMesh(triangleB, sizeof(triangleB));

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        glViewport(0, 0, width, height);
        glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 第一次绘制：明确设置 Program、Uniform 和 VAO。
        glUseProgram(normalProgram);
        glUniform2f(normalOffset, -0.45f, 0.0f);
        glBindVertexArray(meshA.vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        if (!gReusePreviousState) {
            // 正常模式：第二次绘制明确切换全部必要状态。
            glUseProgram(swappedProgram);
            glUniform2f(swappedOffset, 0.45f, 0.0f);
            glBindVertexArray(meshB.vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        else {
            // 实验模式：不切换 Program，也不切换 VAO。
            //
            // 当前状态仍然是：
            //   Program = normalProgram
            //   VAO     = meshA.vao
            //
            // 因此第二次 Draw Call 会再次绘制 triangleA，
            // 只是通过 Uniform 把它移动到了右侧。
            glUniform2f(normalOffset, 0.45f, 0.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        glfwSwapBuffers(window);
    }

    printCurrentState("Before cleanup");

    glDeleteBuffers(1, &meshA.vbo);
    glDeleteVertexArrays(1, &meshA.vao);

    glDeleteBuffers(1, &meshB.vbo);
    glDeleteVertexArrays(1, &meshB.vao);

    glDeleteProgram(normalProgram);
    glDeleteProgram(swappedProgram);

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}