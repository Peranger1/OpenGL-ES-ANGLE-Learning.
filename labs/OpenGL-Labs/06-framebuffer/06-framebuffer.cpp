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
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "%s compile failed:\n%s\n", name, log);
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

static GLuint createProgram(const char* vertexSource, const char* fragmentSource) {
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
        char log[2048];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "program link failed:\n%s\n", log);
        std::exit(EXIT_FAILURE);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

// 窗口尺寸变化后，重新分配离屏颜色纹理和深度缓冲。
static bool resizeFramebuffer(
    GLuint framebuffer,
    GLuint colorTexture,
    GLuint depthRenderbuffer,
    int width,
    int height
) {
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr
    );

    glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);
    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH_COMPONENT16,
        width,
        height
    );

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return status == GL_FRAMEBUFFER_COMPLETE;
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
        glfwCreateWindow(800, 600, "06 - Framebuffer", nullptr, nullptr);

    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    std::printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    std::printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));

    // 第一组 shader：绘制彩色立方体。
    const char* sceneVertexSource = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

uniform mat4 uMvp;

out vec3 vColor;

void main() {
    vColor = aColor;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

    const char* sceneFragmentSource = R"(#version 300 es
precision mediump float;

in vec3 vColor;
out vec4 fragColor;

void main() {
    fragColor = vec4(vColor, 1.0);
}
)";

    // 第二组 shader：把离屏纹理绘制到覆盖整个窗口的矩形。
    const char* screenVertexSource = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;

out vec2 vTexCoord;

void main() {
    vTexCoord = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

    const char* screenFragmentSource = R"(#version 300 es
precision mediump float;

in vec2 vTexCoord;
uniform sampler2D uScreenTexture;

out vec4 fragColor;

void main() {
    vec3 color = texture(uScreenTexture, vTexCoord).rgb;

    // 灰度后处理。删除这一行，直接输出 color，即可恢复原始颜色。
    // float gray = dot(color, vec3(0.299, 0.587, 0.114));

    // 反转颜色
    fragColor = vec4(vec3(1.0) - color, 1.0);
    // fragColor = vec4(vec3(gray), 1.0);
}
)";

    GLuint sceneProgram =
        createProgram(sceneVertexSource, sceneFragmentSource);

    GLuint screenProgram =
        createProgram(screenVertexSource, screenFragmentSource);

    GLint mvpLocation = glGetUniformLocation(sceneProgram, "uMvp");
    GLint screenTextureLocation =
        glGetUniformLocation(screenProgram, "uScreenTexture");

    // 立方体顶点：位置 x, y, z 和颜色 r, g, b。
    float cubeVertices[] = {
        -0.5f, -0.5f, -0.5f,  1.0f, 0.2f, 0.2f,
         0.5f, -0.5f, -0.5f,  0.2f, 1.0f, 0.2f,
         0.5f,  0.5f, -0.5f,  0.2f, 0.4f, 1.0f,
        -0.5f,  0.5f, -0.5f,  1.0f, 1.0f, 0.2f,
        -0.5f, -0.5f,  0.5f,  1.0f, 0.2f, 1.0f,
         0.5f, -0.5f,  0.5f,  0.2f, 1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.6f, 0.2f,
        -0.5f,  0.5f,  0.5f,  0.8f, 0.8f, 0.8f,
    };

    GLuint cubeIndices[] = {
        0, 1, 2,  2, 3, 0,
        4, 5, 6,  6, 7, 4,
        0, 4, 7,  7, 3, 0,
        1, 5, 6,  6, 2, 1,
        3, 2, 6,  6, 7, 3,
        0, 1, 5,  5, 4, 0,
    };

    GLuint cubeVao = 0;
    GLuint cubeVbo = 0;
    GLuint cubeEbo = 0;

    glGenVertexArrays(1, &cubeVao);
    glGenBuffers(1, &cubeVbo);
    glGenBuffers(1, &cubeEbo);

    glBindVertexArray(cubeVao);

    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(cubeVertices),
        cubeVertices,
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cubeEbo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        sizeof(cubeIndices),
        cubeIndices,
        GL_STATIC_DRAW
    );

    glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE,
        6 * sizeof(float),
        reinterpret_cast<void*>(0)
    );
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1, 3, GL_FLOAT, GL_FALSE,
        6 * sizeof(float),
        reinterpret_cast<void*>(3 * sizeof(float))
    );
    glEnableVertexAttribArray(1);

    // 覆盖整个窗口的矩形：位置 x, y 和纹理坐标 u, v。
    float screenVertices[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
    };

    GLuint screenIndices[] = {
        0, 1, 2,
        2, 3, 0,
    };

    GLuint screenVao = 0;
    GLuint screenVbo = 0;
    GLuint screenEbo = 0;

    glGenVertexArrays(1, &screenVao);
    glGenBuffers(1, &screenVbo);
    glGenBuffers(1, &screenEbo);

    glBindVertexArray(screenVao);

    glBindBuffer(GL_ARRAY_BUFFER, screenVbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(screenVertices),
        screenVertices,
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, screenEbo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        sizeof(screenIndices),
        screenIndices,
        GL_STATIC_DRAW
    );

    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<void*>(0)
    );
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE,
        4 * sizeof(float),
        reinterpret_cast<void*>(2 * sizeof(float))
    );
    glEnableVertexAttribArray(1);

    // 创建离屏 Framebuffer Object。
    GLuint framebuffer = 0;
    GLuint colorTexture = 0;
    GLuint depthRenderbuffer = 0;

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    // 颜色输出保存到一张纹理中，后续可供 shader 采样。
    glGenTextures(1, &colorTexture);
    glBindTexture(GL_TEXTURE_2D, colorTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        colorTexture,
        0
    );

    // 深度缓冲只参与遮挡判断，不需要在后处理中采样。
    glGenRenderbuffers(1, &depthRenderbuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRenderbuffer);

    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER,
        depthRenderbuffer
    );

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glUseProgram(screenProgram);
    glUniform1i(screenTextureLocation, 0);

    int framebufferWidth = 0;
    int framebufferHeight = 0;

    while (!glfwWindowShouldClose(window)) {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        if (width == 0 || height == 0) {
            glfwPollEvents();
            continue;
        }

        // 窗口尺寸发生变化时，重新分配离屏缓冲。
        if (width != framebufferWidth || height != framebufferHeight) {
            framebufferWidth = width;
            framebufferHeight = height;

            if (!resizeFramebuffer(
                framebuffer,
                colorTexture,
                depthRenderbuffer,
                width,
                height
            )) {
                std::fprintf(stderr, "framebuffer is incomplete\n");
                return EXIT_FAILURE;
            }
        }

        float time = static_cast<float>(glfwGetTime());

        glm::mat4 model(1.0f);
        model = glm::rotate(model, time, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(
            model,
            time * 0.65f,
            glm::vec3(1.0f, 0.0f, 0.0f)
        );

        glm::mat4 view = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(0.0f, 0.0f, -3.0f)
        );

        glm::mat4 projection = glm::perspective(
            glm::radians(60.0f),
            static_cast<float>(width) / static_cast<float>(height),
            0.1f,
            100.0f
        );

        glm::mat4 mvp = projection * view * model;

        // Pass 1：将彩色立方体绘制到离屏 framebuffer。
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, width, height);

        glEnable(GL_DEPTH_TEST);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(sceneProgram);
        glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, glm::value_ptr(mvp));

        glBindVertexArray(cubeVao);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);

        // Pass 2：把离屏颜色纹理绘制到默认 framebuffer，也就是窗口。
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);

        glDisable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(screenProgram);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, colorTexture);

        glBindVertexArray(screenVao);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteRenderbuffers(1, &depthRenderbuffer);
    glDeleteTextures(1, &colorTexture);
    glDeleteFramebuffers(1, &framebuffer);

    glDeleteBuffers(1, &screenEbo);
    glDeleteBuffers(1, &screenVbo);
    glDeleteVertexArrays(1, &screenVao);

    glDeleteBuffers(1, &cubeEbo);
    glDeleteBuffers(1, &cubeVbo);
    glDeleteVertexArrays(1, &cubeVao);

    glDeleteProgram(screenProgram);
    glDeleteProgram(sceneProgram);

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}