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

struct Vertex {
    glm::vec3 position; // 模型空间坐标
    glm::vec3 normal;   // 模型空间法线
    glm::vec3 color;    // 基础颜色
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

int main() {
    if (!glfwInit()) {
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(
        900,
        700,
        "11 - Shader Lighting",
        nullptr,
        nullptr
    );

    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

// CPU 每帧通过 GLES API 写入这些 Uniform。
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

// Vertex Shader 的输出会经过光栅化插值。
out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec3 vColor;

void main() {
    // 模型空间坐标转换为世界空间坐标。
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);

    vWorldPosition = worldPosition.xyz;
    vWorldNormal = normalize(uNormalMatrix * aNormal);
    vColor = aColor;

    // 世界空间 -> 观察空间 -> 裁剪空间。
    gl_Position = uProjection * uView * worldPosition;
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

// 输入值来自 Vertex Shader，并经过三角形内部插值。
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec3 vColor;

uniform vec3 uLightDirection;
uniform vec3 uCameraPosition;

out vec4 fragColor;

void main() {
    vec3 normal = normalize(vWorldNormal);

    // 方向光的方向指向光线传播方向，因此取负值得到朝向光源的方向。
    vec3 lightDirection = normalize(-uLightDirection);

    // 环境光：即使背对光源，也保留少量亮度。
    float ambientStrength = 0.18;

    // 漫反射：表面越正对光源，亮度越高。
    float diffuseStrength =
        max(dot(normal, lightDirection), 0.0);

    // 镜面高光：观察方向越接近反射方向，高光越明显。
    vec3 viewDirection =
        normalize(uCameraPosition - vWorldPosition);

    vec3 reflectionDirection =
        reflect(-lightDirection, normal);

    float specularStrength = pow(
        max(dot(viewDirection, reflectionDirection), 0.0),
        32.0
    );

    vec3 ambient = ambientStrength * vColor;
    vec3 diffuse = diffuseStrength * vColor;
    vec3 specular = 0.45 * specularStrength * vec3(1.0);

    fragColor = vec4(ambient + diffuse + specular, 1.0);
}
)";

    GLuint program = createProgram(vertexSource, fragmentSource);

    // 每个面使用独立顶点，因为同一个立方体角点在不同面上具有不同法线。
    const Vertex vertices[] = {
        // 前面：normal = (0, 0, 1)
        {{-0.5f, -0.5f,  0.5f}, { 0,  0,  1}, {1.0f, 0.2f, 0.2f}},
        {{ 0.5f, -0.5f,  0.5f}, { 0,  0,  1}, {0.2f, 1.0f, 0.2f}},
        {{ 0.5f,  0.5f,  0.5f}, { 0,  0,  1}, {0.2f, 0.4f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, { 0,  0,  1}, {1.0f, 0.8f, 0.2f}},

        // 后面
        {{ 0.5f, -0.5f, -0.5f}, { 0,  0, -1}, {1.0f, 0.2f, 0.2f}},
        {{-0.5f, -0.5f, -0.5f}, { 0,  0, -1}, {0.2f, 1.0f, 0.2f}},
        {{-0.5f,  0.5f, -0.5f}, { 0,  0, -1}, {0.2f, 0.4f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, { 0,  0, -1}, {1.0f, 0.8f, 0.2f}},

        // 左面
        {{-0.5f, -0.5f, -0.5f}, {-1,  0,  0}, {0.8f, 0.3f, 1.0f}},
        {{-0.5f, -0.5f,  0.5f}, {-1,  0,  0}, {0.8f, 0.3f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {-1,  0,  0}, {0.8f, 0.3f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {-1,  0,  0}, {0.8f, 0.3f, 1.0f}},

        // 右面
        {{ 0.5f, -0.5f,  0.5f}, { 1,  0,  0}, {0.2f, 0.9f, 1.0f}},
        {{ 0.5f, -0.5f, -0.5f}, { 1,  0,  0}, {0.2f, 0.9f, 1.0f}},
        {{ 0.5f,  0.5f, -0.5f}, { 1,  0,  0}, {0.2f, 0.9f, 1.0f}},
        {{ 0.5f,  0.5f,  0.5f}, { 1,  0,  0}, {0.2f, 0.9f, 1.0f}},

        // 顶面
        {{-0.5f,  0.5f,  0.5f}, { 0,  1,  0}, {0.4f, 1.0f, 0.4f}},
        {{ 0.5f,  0.5f,  0.5f}, { 0,  1,  0}, {0.4f, 1.0f, 0.4f}},
        {{ 0.5f,  0.5f, -0.5f}, { 0,  1,  0}, {0.4f, 1.0f, 0.4f}},
        {{-0.5f,  0.5f, -0.5f}, { 0,  1,  0}, {0.4f, 1.0f, 0.4f}},

        // 底面
        {{-0.5f, -0.5f, -0.5f}, { 0, -1,  0}, {1.0f, 0.5f, 0.2f}},
        {{ 0.5f, -0.5f, -0.5f}, { 0, -1,  0}, {1.0f, 0.5f, 0.2f}},
        {{ 0.5f, -0.5f,  0.5f}, { 0, -1,  0}, {1.0f, 0.5f, 0.2f}},
        {{-0.5f, -0.5f,  0.5f}, { 0, -1,  0}, {1.0f, 0.5f, 0.2f}}
    };

    const std::uint16_t indices[] = {
         0,  1,  2,   2,  3,  0,
         4,  5,  6,   6,  7,  4,
         8,  9, 10,  10, 11,  8,
        12, 13, 14,  14, 15, 12,
        16, 17, 18,  18, 19, 16,
        20, 21, 22,  22, 23, 20
    };

    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        sizeof(indices),
        indices,
        GL_STATIC_DRAW
    );

    // position -> Vertex Shader 中的 layout(location = 0)
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, position))
    );
    glEnableVertexAttribArray(0);

    // normal -> Vertex Shader 中的 layout(location = 1)
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, normal))
    );
    glEnableVertexAttribArray(1);

    // color -> Vertex Shader 中的 layout(location = 2)
    glVertexAttribPointer(
        2,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, color))
    );
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);

    // Uniform Location 可以在初始化阶段查询并缓存。
    GLint modelLocation =
        glGetUniformLocation(program, "uModel");

    GLint viewLocation =
        glGetUniformLocation(program, "uView");

    GLint projectionLocation =
        glGetUniformLocation(program, "uProjection");

    GLint normalMatrixLocation =
        glGetUniformLocation(program, "uNormalMatrix");

    GLint lightDirectionLocation =
        glGetUniformLocation(program, "uLightDirection");

    GLint cameraPositionLocation =
        glGetUniformLocation(program, "uCameraPosition");

    glEnable(GL_DEPTH_TEST);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);

        if (width == 0 || height == 0) {
            continue;
        }

        glViewport(0, 0, width, height);
        glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float time = static_cast<float>(glfwGetTime());

        // CPU 使用 GLM 计算矩阵。
        glm::mat4 model(1.0f);
        model = glm::rotate(
            model,
            time * 0.8f,
            glm::vec3(0.0f, 1.0f, 0.0f)
        );
        model = glm::rotate(
            model,
            time * 0.35f,
            glm::vec3(1.0f, 0.0f, 0.0f)
        );

        glm::vec3 cameraPosition(0.0f, 0.4f, 3.0f);

        glm::mat4 view = glm::lookAt(
            cameraPosition,
            glm::vec3(0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

        glm::mat4 projection = glm::perspective(
            glm::radians(60.0f),
            static_cast<float>(width) / static_cast<float>(height),
            0.1f,
            100.0f
        );

        // 法线不能简单使用 Model 矩阵变换。
        // 当模型包含非等比缩放时，需要使用逆矩阵的转置矩阵。
        glm::mat3 normalMatrix =
            glm::transpose(glm::inverse(glm::mat3(model)));

        glUseProgram(program);

        // CPU -> Uniform -> Shader
        glUniformMatrix4fv(
            modelLocation,
            1,
            GL_FALSE,
            glm::value_ptr(model)
        );

        glUniformMatrix4fv(
            viewLocation,
            1,
            GL_FALSE,
            glm::value_ptr(view)
        );

        glUniformMatrix4fv(
            projectionLocation,
            1,
            GL_FALSE,
            glm::value_ptr(projection)
        );

        glUniformMatrix3fv(
            normalMatrixLocation,
            1,
            GL_FALSE,
            glm::value_ptr(normalMatrix)
        );

        glUniform3f(
            lightDirectionLocation,
            -0.7f,
            -1.0f,
            -0.6f
        );

        glUniform3fv(
            cameraPositionLocation,
            1,
            glm::value_ptr(cameraPosition)
        );

        glBindVertexArray(vao);

        glDrawElements(
            GL_TRIANGLES,
            36,
            GL_UNSIGNED_SHORT,
            nullptr
        );

        glfwSwapBuffers(window);
    }

    glDeleteBuffers(1, &ebo);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}