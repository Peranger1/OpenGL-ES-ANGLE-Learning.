#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <cstdio>
#include <cstdlib>

// 检查 shader 是否编译成功。
static void checkShader(GLuint shader, const char* name) {
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok) {
        char log[2048];
        GLsizei length = 0;
        glGetShaderInfoLog(shader, sizeof(log), &length, log);
        std::fprintf(stderr, "%s compile failed:\n%s\n", name, log);
        std::exit(EXIT_FAILURE);
    }
}

// 检查 vertex shader 和 fragment shader 是否链接成功。
static void checkProgram(GLuint program) {
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);

    if (!ok) {
        char log[2048];
        GLsizei length = 0;
        glGetProgramInfoLog(program, sizeof(log), &length, log);
        std::fprintf(stderr, "program link failed:\n%s\n", log);
        std::exit(EXIT_FAILURE);
    }
}

static GLuint compileShader(GLenum type, const char* source, const char* name) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    checkShader(shader, name);
    return shader;
}

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE;
    }

    // 要求 GLFW 通过 EGL 创建 OpenGL ES 3.0 context。
    // 在当前 DLL 配置下，EGL 和 GLES 实现来自 ANGLE。
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window =
        glfwCreateWindow(800, 600, "02 - Indexed Rectangle", nullptr, nullptr);

    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // 打印 ANGLE 版本、实际图形后端和 GPU 信息。
    std::printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
    std::printf("GL_VENDOR   : %s\n", glGetString(GL_VENDOR));

    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec3 vColor;
out vec4 fragColor;

void main() {
    fragColor = vec4(vColor, 1.0);
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
    checkProgram(program);

    // shader 已经进入 program，单独的 shader 对象可以释放。
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // 矩形只需要四个顶点。
    // 每个顶点由 5 个 float 组成：位置 x, y 和颜色 r, g, b。
    //
    //    3 -------- 0
    //    |          |
    //    |          |
    //    2 -------- 1
    //
    float vertices[] = {
        // x      y      r     g     b
         0.5f,  0.5f,  1.0f, 0.0f, 0.0f, // 0: 右上，红色
         0.5f, -0.5f,  0.0f, 1.0f, 0.0f, // 1: 右下，绿色
        -0.5f, -0.5f,  0.0f, 0.0f, 1.0f, // 2: 左下，蓝色
        -0.5f,  0.5f,  1.0f, 1.0f, 0.0f, // 3: 左上，黄色
    };

    // EBO 中存放顶点下标，而不是新的顶点数据。
    // 每三个索引组成一个三角形。
    //
    // 第一个三角形：0 -> 1 -> 3
    // 第二个三角形：1 -> 2 -> 3
    //
    // 顶点 1 和顶点 3 被复用。
    GLuint indices[] = {
        0, 1, 3,
        1, 2, 3,
    };

    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    // VAO 保存顶点输入状态，包括属性格式，以及当前绑定的 EBO。
    glBindVertexArray(vao);

    // VBO 保存实际顶点数据。
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // EBO 保存索引。这个绑定会记录在当前 VAO 中。
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // location 0 对应 shader 中的 aPos。
    // 每个顶点读取两个 float，跨度为五个 float，从偏移 0 开始。
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        reinterpret_cast<void*>(0)
    );
    glEnableVertexAttribArray(0);

    // location 1 对应 shader 中的 aColor。
    // 每个顶点读取三个 float，跳过位置占用的两个 float。
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        reinterpret_cast<void*>(2 * sizeof(float))
    );
    glEnableVertexAttribArray(1);

    while (!glfwWindowShouldClose(window)) {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glBindVertexArray(vao);

        // 根据当前 VAO 记录的 EBO 读取 6 个 GLuint 索引。
        // 6 个索引按每 3 个一组，绘制两个三角形。
        // nullptr 表示从 EBO 的起始位置读取索引。
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // 释放 GPU 和窗口资源。
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}