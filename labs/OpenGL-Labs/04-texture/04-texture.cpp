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

    // 使用 ANGLE 提供的 EGL 和 OpenGL ES 3.0。
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window =
        glfwCreateWindow(800, 600, "04 - Texture", nullptr, nullptr);

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
layout(location = 1) in vec2 aTexCoord;

uniform mat4 uTransform;

// 把每个顶点的 UV 坐标传给 fragment shader。
// 光栅化阶段会自动插值三角形内部像素的 UV。
out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = uTransform * vec4(aPos, 0.0, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

// 接收经过插值的 UV 坐标。
in vec2 vTexCoord;

// sampler2D 表示一个二维纹理采样器。
// CPU 会把它绑定到某个 texture unit。
uniform sampler2D uTexture;

out vec4 fragColor;

void main() {
    // 根据 UV 坐标从纹理中读取颜色。
    fragColor = texture(uTexture, vTexCoord);
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

    GLint transformLocation = glGetUniformLocation(program, "uTransform");
    GLint textureLocation = glGetUniformLocation(program, "uTexture");

    if (transformLocation == -1 || textureLocation == -1) {
        std::fprintf(stderr, "cannot find shader uniform\n");
        return EXIT_FAILURE;
    }

    // 每个顶点由四个 float 组成：
    //
    // x, y -> 顶点位置
    // u, v -> 纹理坐标
    //
    // UV 通常位于 0.0 到 1.0 之间：
    //
    //    3 (0, 1) -------- (1, 1) 0
    //       |                     |
    //       |                     |
    //    2 (0, 0) -------- (1, 0) 1
    //
    float vertices[] = {
        // x      y      u     v
         0.5f,  0.5f,  2.0f, 2.0f, // 0: 右上
         0.5f, -0.5f,  2.0f, 0.0f, // 1: 右下
        -0.5f, -0.5f,  0.0f, 0.0f, // 2: 左下
        -0.5f,  0.5f,  0.0f, 2.0f, // 3: 左上
    };

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

    // location 0: 读取位置 x, y。
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<void*>(0)
    );
    glEnableVertexAttribArray(0);

    // location 1: 跳过位置数据，读取纹理坐标 u, v。
    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<void*>(2 * sizeof(float))
    );
    glEnableVertexAttribArray(1);

    // 这是一张 4x4 RGBA 棋盘纹理。
    // 每四个 unsigned char 表示一个像素：r, g, b, a。
    //
    // 为了容易观察纹理方向，左上角使用红色。
    const unsigned char pixels[] = {
        // 第 0 行：底部
        245, 245, 245, 255,   40,  45,  55, 255,  245, 245, 245, 255,   40,  45,  55, 255,
         40,  45,  55, 255,  245, 245, 245, 255,   40,  45,  55, 255,  245, 245, 245, 255,
        245, 245, 245, 255,   40,  45,  55, 255,  245, 245, 245, 255,   40,  45,  55, 255,

        // 第 3 行：顶部。左上角改成红色，方便确认 UV 方向。
        230,  55,  70, 255,  245, 245, 245, 255,   40,  45,  55, 255,  245, 245, 245, 255,
    };

    GLuint texture = 0;
    glGenTextures(1, &texture);

    // 绑定 texture 后，后续纹理配置都会作用在这个对象上。
    glBindTexture(GL_TEXTURE_2D, texture);

    // 把 CPU 内存中的像素上传到 GPU。
    //
    // GL_RGBA：每个像素包含 RGBA 四个通道。
    // GL_UNSIGNED_BYTE：每个通道占用一个字节，范围为 0 到 255。
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        4,
        4,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        pixels
    );

    // 当纹理被缩小或放大时，使用最近邻采样。
    // 这样棋盘格边界比较清晰，便于观察单个 texel。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // UV 超出 0.0 到 1.0 时重复纹理。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // 告诉 shader：uTexture 从 texture unit 0 读取纹理。
    glUseProgram(program);
    glUniform1i(textureLocation, 0);

    while (!glfwWindowShouldClose(window)) {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        float angle = static_cast<float>(glfwGetTime());
        float cosine = std::cos(angle);
        float sine = std::sin(angle);

        float aspectScale = 1.0f;

        if (width > 0) {
            aspectScale =
                static_cast<float>(height) / static_cast<float>(width);
        }

        float transform[] = {
             aspectScale * cosine, sine,   0.0f, 0.0f,
            -aspectScale * sine,   cosine, 0.0f, 0.0f,
             0.0f,                 0.0f,   1.0f, 0.0f,
             0.0f,                 0.0f,   0.0f, 1.0f,
        };

        glUseProgram(program);
        glUniformMatrix4fv(transformLocation, 1, GL_FALSE, transform);

        // OpenGL ES 提供多个 texture unit。
        // 激活 unit 0，再把 texture 绑定到这个 unit。
        // 它与之前 glUniform1i(textureLocation, 0) 中的 0 对应。
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteTextures(1, &texture);
    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}