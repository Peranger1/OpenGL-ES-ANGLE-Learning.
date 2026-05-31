#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <cstdlib>

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

static GLuint compileShader(
    GLenum type,
    const char* source,
    const char* name
) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    checkShader(shader, name);
    return shader;
}

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

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window =
        glfwCreateWindow(800, 600, "05 - Depth 3D Cube", nullptr, nullptr);

    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    std::printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
    std::printf("GL_VENDOR   : %s\n", glGetString(GL_VENDOR));

    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

// MVP = Projection * View * Model。
uniform mat4 uMvp;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = uMvp * vec4(aPos, 1.0);
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

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint mvpLocation = glGetUniformLocation(program, "uMvp");

    if (mvpLocation == -1) {
        std::fprintf(stderr, "cannot find uniform: uMvp\n");
        return EXIT_FAILURE;
    }

    // 立方体有 8 个顶点。
    // 每个顶点由位置 x, y, z 和颜色 r, g, b 组成。
    float vertices[] = {
        // x      y      z      r     g     b
        -0.5f, -0.5f, -0.5f,  1.0f, 0.2f, 0.2f, // 0
         0.5f, -0.5f, -0.5f,  0.2f, 1.0f, 0.2f, // 1
         0.5f,  0.5f, -0.5f,  0.2f, 0.4f, 1.0f, // 2
        -0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.2f, // 3
        -0.5f, -0.5f,  0.5f,  1.0f, 0.2f, 1.0f, // 4
         0.5f, -0.5f,  0.5f,  0.2f, 1.0f, 1.0f, // 5
         0.5f,  0.5f,  0.5f,  1.0f, 0.6f, 0.2f, // 6
        -0.5f,  0.5f,  0.5f,  0.8f, 0.8f, 0.8f, // 7
    };

    // 六个面，每个面由两个三角形组成。
    // 每个三角形需要三个索引，共 36 个索引。
    GLuint indices[] = {
        0, 1, 2,  2, 3, 0, // 后面
        4, 5, 6,  6, 7, 4, // 前面
        0, 4, 7,  7, 3, 0, // 左面
        1, 5, 6,  6, 2, 1, // 右面
        3, 2, 6,  6, 7, 3, // 上面
        0, 1, 5,  5, 4, 0, // 下面
    };

    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // location 0：读取位置 x, y, z。
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        6 * sizeof(float),
        reinterpret_cast<void*>(0)
    );
    glEnableVertexAttribArray(0);

    // location 1：跳过位置，读取颜色 r, g, b。
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        6 * sizeof(float),
        reinterpret_cast<void*>(3 * sizeof(float))
    );
    glEnableVertexAttribArray(1);

    // 开启深度测试。
    // GPU 会记录每个像素当前最近的深度值。
    // 新片段如果位于已有片段后面，就不会覆盖前面的颜色。
    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window)) {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        // 窗口最小化时，避免除以 0。
        if (width == 0 || height == 0) {
            glfwPollEvents();
            continue;
        }

        glViewport(0, 0, width, height);

        // 除了颜色缓冲，还必须每帧清空深度缓冲。
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float time = static_cast<float>(glfwGetTime());

        // Model：从单位矩阵开始，让立方体依次绕 Y 轴和 X 轴旋转。
        glm::mat4 model(1.0f);
        model = glm::rotate(model, time, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, time * 0.65f, glm::vec3(1.0f, 0.0f, 0.0f));

        // View：把场景向屏幕内部移动 3 个单位。
        // 这等价于相机位于 z = 3 的位置，朝向原点观察。
        glm::mat4 view = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(0.0f, 0.0f, -3.0f)
        );

        // Projection：使用 60 度垂直视野的透视投影。
        glm::mat4 projection = glm::perspective(
            glm::radians(60.0f),
            static_cast<float>(width) / static_cast<float>(height),
            0.1f,
            100.0f
        );

        // 顶点实际执行顺序：Model -> View -> Projection。
        glm::mat4 mvp = projection * view * model;

        glUseProgram(program);

        // GLM 默认使用 OpenGL 期望的列优先布局。
        // value_ptr() 返回矩阵首元素地址，供 OpenGL ES 上传数据。
        glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, glm::value_ptr(mvp));

        glBindVertexArray(vao);

        // 读取 36 个索引，绘制 12 个三角形。
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}
