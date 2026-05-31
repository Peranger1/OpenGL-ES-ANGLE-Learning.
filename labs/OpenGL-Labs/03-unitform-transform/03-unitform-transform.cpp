#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <cmath>
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

    // 请求使用 EGL 创建 OpenGL ES 3.0 context。
    // 当前 EGL 和 GLES 实现由 ANGLE 提供。
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window =
        glfwCreateWindow(800, 600, "03 - Uniform Transform", nullptr, nullptr);

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
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec3 aColor;

        // uniform 是 CPU 每帧传给 shader 的数据。
        // 它对当前 draw call 中的所有顶点保持一致。
        uniform mat4 uTransform;

        out vec3 vColor;

        void main() {
            vColor = aColor;

            // 把二维顶点扩展为齐次坐标，再乘以变换矩阵。
            gl_Position = uTransform * vec4(aPos, 0.0, 1.0);
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

    // 查询 shader 中 uniform 变量的位置。
    // 后续通过这个位置更新 uTransform。
    GLint transformLocation = glGetUniformLocation(program, "uTransform");

    if (transformLocation == -1) {
        std::fprintf(stderr, "cannot find uniform: uTransform\n");
        return EXIT_FAILURE;
    }

    // 每个顶点由位置 x, y 和颜色 r, g, b 组成。
    //
    //    3 -------- 0
    //    |          |
    //    |          |
    //    2 -------- 1
    //
    float vertices[] = {
        // x      y      r     g     b
         0.5f,  0.5f,  1.0f, 0.0f, 0.0f, // 0: 右上
         0.5f, -0.5f,  0.0f, 1.0f, 0.0f, // 1: 右下
        -0.5f, -0.5f,  0.0f, 0.0f, 1.0f, // 2: 左下
        -0.5f,  0.5f,  1.0f, 1.0f, 0.0f, // 3: 左上
    };

    // 使用索引复用矩形的四个顶点。
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

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // location 0: 每个顶点读取两个 float 作为位置。
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        reinterpret_cast<void*>(0)
    );
    glEnableVertexAttribArray(0);

    // location 1: 跳过位置数据，读取三个 float 作为颜色。
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

        // glfwGetTime() 返回 glfwInit() 之后经过的秒数。
        // 将时间直接作为弧度值，矩形就会持续旋转。
        float angle = static_cast<float>(glfwGetTime());
        float cosine = std::cos(angle);
        float sine = std::sin(angle);

        // 窗口通常不是正方形。
        // 对 x 方向做缩放，可以避免矩形随窗口比例拉伸。
        float aspectScale = 1.0f;

        if (width > 0) {
            aspectScale =
                static_cast<float>(height) / static_cast<float>(width);
        }

        // OpenGL 使用列优先顺序读取矩阵。
        //
        // 这个矩阵等价于：
        //
        //     | scaleX * cos  -scaleX * sin  0  0 |
        //     |          sin            cos  0  0 |
        //     |            0              0  1  0 |
        //     |            0              0  0  1 |
        //
        // 它先让矩形旋转，再修正窗口宽高比。
        float transform[] = {
             aspectScale * cosine, sine,   0.0f, 0.0f,
            -aspectScale * sine,   cosine, 0.0f, 0.0f,
             0.0f,                 0.0f,   1.0f, 0.0f,
             0.0f,                 0.0f,   0.0f, 1.0f,
        };

        glUseProgram(program);

        // 把 CPU 中的 4x4 矩阵传给当前 program 的 uTransform。
        //
        // 第二个参数 1：上传一个矩阵。
        // GL_FALSE：不要求 OpenGL 转置矩阵。
        glUniformMatrix4fv(transformLocation, 1, GL_FALSE, transform);

        glBindVertexArray(vao);

        // 根据 EBO 中的六个索引绘制两个三角形。
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

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