#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <cstdio>
#include <cstdlib>

static void checkShader(GLuint shader, const char* name) {
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    if (!ok) {
        char log[2048];
        GLsizei len = 0;
        glGetShaderInfoLog(shader, sizeof(log), &len, log);
        std::fprintf(stderr, "%s compile failed:\n%s\n", name, log);
        std::exit(1);
    }
}

static void checkProgram(GLuint program) {
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);

    if (!ok) {
        char log[2048];
        GLsizei len = 0;
        glGetProgramInfoLog(program, sizeof(log), &len, log);
        std::fprintf(stderr, "program link failed:\n%s\n", log);
        std::exit(1);
    }
}

static GLuint compileShader(GLenum type, const char* source, const char* name) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    checkShader(shader, name);
    return shader;
}

int main(int argc, char* argv[]) {
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(800, 600, "GLFW + GLES3 Triangle", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    std::printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
    std::printf("GL_VENDOR   : %s\n", glGetString(GL_VENDOR));

    /*
    * 顶点数据
          -> Vertex Shader：处理每个顶点的位置和颜色
          -> 光栅化：把三角形转换成像素，并插值颜色
          -> Fragment Shader：计算每个像素最终显示的颜色
    */
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

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSource, "vertex shader");
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSource, "fragment shader");

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    checkProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    float vertices[] = {
        // x      y      r     g     b
         0.0f,  0.6f,  1.0f, 0.2f, 0.2f,
        -0.6f, -0.5f,  0.2f, 1.0f, 0.2f,
         0.6f, -0.5f,  0.2f, 0.4f, 1.0f,
    };

    GLuint vao = 0;
    GLuint vbo = 0;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // location = 0
    // 每个顶点读取 2 个 float
    // 类型是 GL_FLOAT
    // 不需要归一化
    // 每个顶点总跨度是 5 个 float
    // 从每个顶点的第 0 个 float 开始读
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // location = 1
    // 每个顶点读取 3 个 float
    // 类型是 GL_FLOAT
    // 不需要归一化
    // 每个顶点总跨度是 5 个 float
    // 从每个顶点的第 2 个 float 开始读
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
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
        // 从当前绑定的 VAO/VBO 中，按顺序取 3 个顶点，组成 1 个三角形并绘制。
        // GL_TRIANGLES：每 3 个顶点组成一个独立三角形
        // 0           ：从第 0 个顶点开始读
        // 3           ：一共读取 3 个顶点
        glDrawArrays(GL_TRIANGLES, 0, 3);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}