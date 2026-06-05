#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

struct Renderer {
    GLuint skyboxProgram = 0;
    GLuint reflectProgram = 0;
    GLuint cubemap = 0;

    Mesh skyboxCube;
    Mesh reflectiveCube;

    GLint skyboxViewLocation = -1;
    GLint skyboxProjectionLocation = -1;

    GLint reflectModelLocation = -1;
    GLint reflectViewLocation = -1;
    GLint reflectProjectionLocation = -1;
    GLint reflectNormalMatrixLocation = -1;
    GLint reflectCameraPositionLocation = -1;
};

static GLuint compileShader(GLenum type, const char* source, const char* name) {
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

static GLuint linkProgram(
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

static GLuint createSkyboxProgram() {
    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec3 aPosition;

uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vDirection;

void main() {
    // Cube Map 使用方向向量采样。
    // 对天空盒来说，立方体顶点位置本身就可以当作采样方向。
    vDirection = aPosition;

    vec4 clipPosition =
        uProjection * uView * vec4(aPosition, 1.0);

    // 将 z 设置为 w，使天空盒深度始终接近最远处。
    gl_Position = clipPosition.xyww;
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec3 vDirection;

uniform samplerCube uSkybox;

out vec4 fragColor;

void main() {
    // samplerCube 不使用 vec2 UV，而使用 vec3 方向。
    fragColor = texture(uSkybox, normalize(vDirection));
}
)";

    return linkProgram(vertexSource, fragmentSource);
}

static GLuint createReflectProgram() {
    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out vec3 vWorldPosition;
out vec3 vWorldNormal;

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);

    vWorldPosition = worldPosition.xyz;
    vWorldNormal = normalize(uNormalMatrix * aNormal);

    gl_Position = uProjection * uView * worldPosition;
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec3 vWorldPosition;
in vec3 vWorldNormal;

uniform vec3 uCameraPosition;
uniform samplerCube uSkybox;

out vec4 fragColor;

void main() {
    vec3 normal = normalize(vWorldNormal);

    // 从相机指向片元的视线方向。
    vec3 viewDirection =
        normalize(vWorldPosition - uCameraPosition);

    // reflect(I, N) 表示入射方向 I 关于法线 N 的反射方向。
    vec3 reflectedDirection =
        reflect(viewDirection, normal);

    vec3 reflectedColor =
        texture(uSkybox, reflectedDirection).rgb;

    fragColor = vec4(reflectedColor, 1.0);
}
)";

    return linkProgram(vertexSource, fragmentSource);
}

static Mesh createSkyboxCube() {
    const float vertices[] = {
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f
    };

    const std::uint16_t indices[] = {
        0, 2, 1,  2, 0, 3,
        4, 5, 6,  6, 7, 4,
        0, 4, 7,  7, 3, 0,
        1, 2, 6,  6, 5, 1,
        3, 7, 6,  6, 2, 3,
        0, 1, 5,  5, 4, 0
    };

    Mesh mesh;
    mesh.indexCount = 36;

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

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

static Mesh createReflectiveCube() {
    // 每个面使用独立顶点，因为不同面需要不同法线。
    const float vertices[] = {
        // position             // normal
        -0.5f, -0.5f,  0.5f,    0.0f,  0.0f,  1.0f,
         0.5f, -0.5f,  0.5f,    0.0f,  0.0f,  1.0f,
         0.5f,  0.5f,  0.5f,    0.0f,  0.0f,  1.0f,
        -0.5f,  0.5f,  0.5f,    0.0f,  0.0f,  1.0f,

         0.5f, -0.5f, -0.5f,    0.0f,  0.0f, -1.0f,
        -0.5f, -0.5f, -0.5f,    0.0f,  0.0f, -1.0f,
        -0.5f,  0.5f, -0.5f,    0.0f,  0.0f, -1.0f,
         0.5f,  0.5f, -0.5f,    0.0f,  0.0f, -1.0f,

        -0.5f, -0.5f, -0.5f,   -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f,  0.5f,   -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f,  0.5f,   -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f, -0.5f,   -1.0f,  0.0f,  0.0f,

         0.5f, -0.5f,  0.5f,    1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f,    1.0f,  0.0f,  0.0f,
         0.5f,  0.5f, -0.5f,    1.0f,  0.0f,  0.0f,
         0.5f,  0.5f,  0.5f,    1.0f,  0.0f,  0.0f,

        -0.5f,  0.5f,  0.5f,    0.0f,  1.0f,  0.0f,
         0.5f,  0.5f,  0.5f,    0.0f,  1.0f,  0.0f,
         0.5f,  0.5f, -0.5f,    0.0f,  1.0f,  0.0f,
        -0.5f,  0.5f, -0.5f,    0.0f,  1.0f,  0.0f,

        -0.5f, -0.5f, -0.5f,    0.0f, -1.0f,  0.0f,
         0.5f, -0.5f, -0.5f,    0.0f, -1.0f,  0.0f,
         0.5f, -0.5f,  0.5f,    0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f,  0.5f,    0.0f, -1.0f,  0.0f
    };

    const std::uint16_t indices[] = {
         0,  1,  2,   2,  3,  0,
         4,  5,  6,   6,  7,  4,
         8,  9, 10,  10, 11,  8,
        12, 13, 14,  14, 15, 12,
        16, 17, 18,  18, 19, 16,
        20, 21, 22,  22, 23, 20
    };

    Mesh mesh;
    mesh.indexCount = 36;

    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);

    glBindVertexArray(mesh.vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        6 * sizeof(float),
        nullptr
    );
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        6 * sizeof(float),
        reinterpret_cast<void*>(3 * sizeof(float))
    );
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    return mesh;
}

static void fillFacePixels(
    std::vector<std::uint8_t>& pixels,
    int size,
    const glm::vec3& baseColor,
    int faceIndex
) {
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float u = static_cast<float>(x) / static_cast<float>(size - 1);
            float v = static_cast<float>(y) / static_cast<float>(size - 1);

            float checker =
                ((x / 16 + y / 16) % 2 == 0) ? 1.0f : 0.72f;

            glm::vec3 color =
                baseColor * checker +
                glm::vec3(u * 0.18f, v * 0.18f, faceIndex * 0.025f);

            int offset = (y * size + x) * 4;

            pixels[offset + 0] =
                static_cast<std::uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);

            pixels[offset + 1] =
                static_cast<std::uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);

            pixels[offset + 2] =
                static_cast<std::uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);

            pixels[offset + 3] = 255;
        }
    }
}

static GLuint createProceduralCubemap() {
    constexpr int size = 128;

    const glm::vec3 faceColors[6] = {
        glm::vec3(1.00f, 0.20f, 0.20f), // +X
        glm::vec3(0.20f, 1.00f, 0.25f), // -X
        glm::vec3(0.25f, 0.40f, 1.00f), // +Y
        glm::vec3(1.00f, 0.85f, 0.20f), // -Y
        glm::vec3(1.00f, 0.35f, 0.95f), // +Z
        glm::vec3(0.20f, 0.95f, 1.00f)  // -Z
    };

    GLuint cubemap = 0;
    glGenTextures(1, &cubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap);

    std::vector<std::uint8_t> pixels(size * size * 4);

    for (int face = 0; face < 6; ++face) {
        fillFacePixels(pixels, size, faceColors[face], face);

        glTexImage2D(
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            0,
            GL_RGBA,
            size,
            size,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixels.data()
        );
    }

    // Cube Map 的 6 个面尺寸和格式必须匹配，否则采样可能不完整。
    glTexParameteri(
        GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_MIN_FILTER,
        GL_LINEAR
    );

    glTexParameteri(
        GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_MAG_FILTER,
        GL_LINEAR
    );

    // Cube Map 通常使用 CLAMP_TO_EDGE，避免各面边缘采样时出现接缝。
    glTexParameteri(
        GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_WRAP_S,
        GL_CLAMP_TO_EDGE
    );

    glTexParameteri(
        GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE
    );

    glTexParameteri(
        GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_WRAP_R,
        GL_CLAMP_TO_EDGE
    );

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    return cubemap;
}

static Renderer createRenderer() {
    Renderer renderer;

    renderer.skyboxProgram = createSkyboxProgram();
    renderer.reflectProgram = createReflectProgram();

    renderer.skyboxCube = createSkyboxCube();
    renderer.reflectiveCube = createReflectiveCube();

    renderer.cubemap = createProceduralCubemap();

    renderer.skyboxViewLocation =
        glGetUniformLocation(renderer.skyboxProgram, "uView");

    renderer.skyboxProjectionLocation =
        glGetUniformLocation(renderer.skyboxProgram, "uProjection");

    renderer.reflectModelLocation =
        glGetUniformLocation(renderer.reflectProgram, "uModel");

    renderer.reflectViewLocation =
        glGetUniformLocation(renderer.reflectProgram, "uView");

    renderer.reflectProjectionLocation =
        glGetUniformLocation(renderer.reflectProgram, "uProjection");

    renderer.reflectNormalMatrixLocation =
        glGetUniformLocation(renderer.reflectProgram, "uNormalMatrix");

    renderer.reflectCameraPositionLocation =
        glGetUniformLocation(renderer.reflectProgram, "uCameraPosition");

    glUseProgram(renderer.skyboxProgram);
    glUniform1i(
        glGetUniformLocation(renderer.skyboxProgram, "uSkybox"),
        0
    );

    glUseProgram(renderer.reflectProgram);
    glUniform1i(
        glGetUniformLocation(renderer.reflectProgram, "uSkybox"),
        0
    );

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    return renderer;
}

static void drawReflectiveCube(
    const Renderer& renderer,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPosition,
    float elapsedSeconds
) {
    glm::mat4 model(1.0f);

    model = glm::rotate(
        model,
        elapsedSeconds * 0.7f,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    model = glm::rotate(
        model,
        elapsedSeconds * 0.35f,
        glm::vec3(1.0f, 0.0f, 0.0f)
    );

    glm::mat3 normalMatrix =
        glm::transpose(glm::inverse(glm::mat3(model)));

    glUseProgram(renderer.reflectProgram);

    glUniformMatrix4fv(
        renderer.reflectModelLocation,
        1,
        GL_FALSE,
        glm::value_ptr(model)
    );

    glUniformMatrix4fv(
        renderer.reflectViewLocation,
        1,
        GL_FALSE,
        glm::value_ptr(view)
    );

    glUniformMatrix4fv(
        renderer.reflectProjectionLocation,
        1,
        GL_FALSE,
        glm::value_ptr(projection)
    );

    glUniformMatrix3fv(
        renderer.reflectNormalMatrixLocation,
        1,
        GL_FALSE,
        glm::value_ptr(normalMatrix)
    );

    glUniform3fv(
        renderer.reflectCameraPositionLocation,
        1,
        glm::value_ptr(cameraPosition)
    );

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, renderer.cubemap);

    glBindVertexArray(renderer.reflectiveCube.vao);

    glDrawElements(
        GL_TRIANGLES,
        renderer.reflectiveCube.indexCount,
        GL_UNSIGNED_SHORT,
        nullptr
    );
}

static void drawSkybox(
    const Renderer& renderer,
    const glm::mat4& view,
    const glm::mat4& projection
) {
    // 天空盒应该始终在最远处。
    // 先绘制真实物体，再绘制天空盒，可以减少天空盒片元开销。
    glDepthFunc(GL_LEQUAL);

    // 天空盒不应该写入深度缓冲。
    glDepthMask(GL_FALSE);

    // 天空盒是从立方体内部看，关闭裁剪更简单。
    glDisable(GL_CULL_FACE);

    // 去掉 View 矩阵的平移，只保留相机旋转。
    // 这样相机移动时天空盒不会跟着产生视差。
    glm::mat4 viewNoTranslation =
        glm::mat4(glm::mat3(view));

    glUseProgram(renderer.skyboxProgram);

    glUniformMatrix4fv(
        renderer.skyboxViewLocation,
        1,
        GL_FALSE,
        glm::value_ptr(viewNoTranslation)
    );

    glUniformMatrix4fv(
        renderer.skyboxProjectionLocation,
        1,
        GL_FALSE,
        glm::value_ptr(projection)
    );

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, renderer.cubemap);

    glBindVertexArray(renderer.skyboxCube.vao);

    glDrawElements(
        GL_TRIANGLES,
        renderer.skyboxCube.indexCount,
        GL_UNSIGNED_SHORT,
        nullptr
    );

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
}

static void renderFrame(
    const Renderer& renderer,
    int width,
    int height,
    float elapsedSeconds
) {
    glViewport(0, 0, width, height);

    glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::vec3 cameraPosition(
        std::sin(elapsedSeconds * 0.25f) * 1.3f,
        0.45f,
        3.4f
    );

    glm::mat4 view = glm::lookAt(
        cameraPosition,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    glm::mat4 projection = glm::perspective(
        glm::radians(60.0f),
        static_cast<float>(width) / static_cast<float>(height),
        0.1f,
        100.0f
    );

    drawReflectiveCube(
        renderer,
        view,
        projection,
        cameraPosition,
        elapsedSeconds
    );

    drawSkybox(
        renderer,
        view,
        projection
    );
}

static void destroyMesh(Mesh& mesh) {
    glDeleteBuffers(1, &mesh.ebo);
    glDeleteBuffers(1, &mesh.vbo);
    glDeleteVertexArrays(1, &mesh.vao);
}

static void destroyRenderer(Renderer& renderer) {
    destroyMesh(renderer.reflectiveCube);
    destroyMesh(renderer.skyboxCube);

    glDeleteTextures(1, &renderer.cubemap);

    glDeleteProgram(renderer.reflectProgram);
    glDeleteProgram(renderer.skyboxProgram);
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
        1000,
        720,
        "17 - Cubemap",
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

    Renderer renderer = createRenderer();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        int width = 0;
        int height = 0;

        glfwGetFramebufferSize(window, &width, &height);

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