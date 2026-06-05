#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

constexpr int SHADOW_SIZE = 1024;

struct Mesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    GLsizei indexCount = 0;
};

struct ShadowTarget {
    GLuint framebuffer = 0;
    GLuint depthTexture = 0;
    GLuint dummyColorRenderbuffer = 0;
};

struct Renderer {
    GLuint depthProgram = 0;
    GLuint sceneProgram = 0;

    Mesh cube;
    Mesh plane;

    ShadowTarget shadow;

    GLint depthModelLocation = -1;
    GLint depthLightViewProjectionLocation = -1;

    GLint sceneModelLocation = -1;
    GLint sceneViewLocation = -1;
    GLint sceneProjectionLocation = -1;
    GLint sceneLightViewProjectionLocation = -1;
    GLint sceneNormalMatrixLocation = -1;
    GLint sceneColorLocation = -1;
    GLint sceneLightDirectionLocation = -1;
    GLint sceneShadowMapLocation = -1;
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

static GLuint createDepthProgram() {
    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uLightViewProjection;

void main() {
    gl_Position =
        uLightViewProjection *
        uModel *
        vec4(aPosition, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

out vec4 dummyColor;

void main() {
    // 深度值由固定管线写入 depth attachment。
    // 这里写一个无意义颜色，只是为了让带颜色附件的 FBO 保持简单。
    dummyColor = vec4(0.0);
}
)";

    return linkProgram(vertexSource, fragmentSource);
}

static GLuint createSceneProgram() {
    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightViewProjection;
uniform mat3 uNormalMatrix;

out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec4 vLightClipPosition;

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);

    vWorldPosition = worldPosition.xyz;
    vWorldNormal = normalize(uNormalMatrix * aNormal);

    // 当前顶点在光源裁剪空间中的位置。
    // Fragment Shader 会用它转换到 shadow map 的 UV。
    vLightClipPosition = uLightViewProjection * worldPosition;

    gl_Position = uProjection * uView * worldPosition;
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec4 vLightClipPosition;

uniform vec3 uColor;
uniform vec3 uLightDirection;
uniform sampler2D uShadowMap;

out vec4 fragColor;

float calculateShadow(vec4 lightClipPosition, vec3 normal) {
    // 裁剪空间 -> NDC。
    vec3 projected = lightClipPosition.xyz / lightClipPosition.w;

    // NDC -1..1 -> Texture UV 0..1。
    vec3 shadowCoord = projected * 0.5 + 0.5;

    // 超出 shadow map 范围时，不认为它在阴影里。
    if (
        shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
        shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
        shadowCoord.z < 0.0 || shadowCoord.z > 1.0
    ) {
        return 0.0;
    }

    float closestDepth =
        texture(uShadowMap, shadowCoord.xy).r;

    float currentDepth =
        shadowCoord.z;

    // bias 用于缓解 shadow acne。
    // 表面越斜，bias 稍微越大。
    vec3 lightDir = normalize(-uLightDirection);

    float bias = max(
        0.006 * (1.0 - dot(normal, lightDir)),
        0.0018
    );

    return currentDepth - bias > closestDepth ? 1.0 : 0.0;
}

void main() {
    vec3 normal = normalize(vWorldNormal);
    vec3 lightDir = normalize(-uLightDirection);

    float diffuse =
        max(dot(normal, lightDir), 0.0);

    float shadow =
        calculateShadow(vLightClipPosition, normal);

    vec3 ambient =
        uColor * 0.22;

    vec3 lit =
        uColor * diffuse * 0.85;

    // 阴影区域保留 ambient，削弱直接光。
    vec3 finalColor =
        ambient + lit * (1.0 - shadow * 0.78);

    fragColor = vec4(finalColor, 1.0);
}
)";

    return linkProgram(vertexSource, fragmentSource);
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
    glBufferData(
        GL_ARRAY_BUFFER,
        vertexBytes,
        vertices,
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        indexBytes,
        indices,
        GL_STATIC_DRAW
    );

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

static Mesh createCubeMesh() {
    const float vertices[] = {
        // position              // normal
        -0.5f,-0.5f, 0.5f,       0.0f, 0.0f, 1.0f,
         0.5f,-0.5f, 0.5f,       0.0f, 0.0f, 1.0f,
         0.5f, 0.5f, 0.5f,       0.0f, 0.0f, 1.0f,
        -0.5f, 0.5f, 0.5f,       0.0f, 0.0f, 1.0f,

         0.5f,-0.5f,-0.5f,       0.0f, 0.0f,-1.0f,
        -0.5f,-0.5f,-0.5f,       0.0f, 0.0f,-1.0f,
        -0.5f, 0.5f,-0.5f,       0.0f, 0.0f,-1.0f,
         0.5f, 0.5f,-0.5f,       0.0f, 0.0f,-1.0f,

        -0.5f,-0.5f,-0.5f,      -1.0f, 0.0f, 0.0f,
        -0.5f,-0.5f, 0.5f,      -1.0f, 0.0f, 0.0f,
        -0.5f, 0.5f, 0.5f,      -1.0f, 0.0f, 0.0f,
        -0.5f, 0.5f,-0.5f,      -1.0f, 0.0f, 0.0f,

         0.5f,-0.5f, 0.5f,       1.0f, 0.0f, 0.0f,
         0.5f,-0.5f,-0.5f,       1.0f, 0.0f, 0.0f,
         0.5f, 0.5f,-0.5f,       1.0f, 0.0f, 0.0f,
         0.5f, 0.5f, 0.5f,       1.0f, 0.0f, 0.0f,

        -0.5f, 0.5f, 0.5f,       0.0f, 1.0f, 0.0f,
         0.5f, 0.5f, 0.5f,       0.0f, 1.0f, 0.0f,
         0.5f, 0.5f,-0.5f,       0.0f, 1.0f, 0.0f,
        -0.5f, 0.5f,-0.5f,       0.0f, 1.0f, 0.0f,

        -0.5f,-0.5f,-0.5f,       0.0f,-1.0f, 0.0f,
         0.5f,-0.5f,-0.5f,       0.0f,-1.0f, 0.0f,
         0.5f,-0.5f, 0.5f,       0.0f,-1.0f, 0.0f,
        -0.5f,-0.5f, 0.5f,       0.0f,-1.0f, 0.0f
    };

    const std::uint16_t indices[] = {
         0, 1, 2,   2, 3, 0,
         4, 5, 6,   6, 7, 4,
         8, 9,10,  10,11, 8,
        12,13,14,  14,15,12,
        16,17,18,  18,19,16,
        20,21,22,  22,23,20
    };

    return createMesh(
        vertices,
        sizeof(vertices),
        indices,
        sizeof(indices),
        36
    );
}

static Mesh createPlaneMesh() {
    const float vertices[] = {
        // position              // normal
        -8.0f, -0.65f, -8.0f,    0.0f, 1.0f, 0.0f,
         8.0f, -0.65f, -8.0f,    0.0f, 1.0f, 0.0f,
         8.0f, -0.65f,  8.0f,    0.0f, 1.0f, 0.0f,
        -8.0f, -0.65f,  8.0f,    0.0f, 1.0f, 0.0f
    };

    // 从 +Y 方向看是逆时针。
    const std::uint16_t indices[] = {
        0, 2, 1,
        2, 0, 3
    };

    return createMesh(
        vertices,
        sizeof(vertices),
        indices,
        sizeof(indices),
        6
    );
}

static ShadowTarget createShadowTarget() {
    ShadowTarget target;

    glGenFramebuffers(1, &target.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);

    glGenTextures(1, &target.depthTexture);
    glBindTexture(GL_TEXTURE_2D, target.depthTexture);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_DEPTH_COMPONENT24,
        SHADOW_SIZE,
        SHADOW_SIZE,
        0,
        GL_DEPTH_COMPONENT,
        GL_UNSIGNED_INT,
        nullptr
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MIN_FILTER,
        GL_NEAREST
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_MAG_FILTER,
        GL_NEAREST
    );

    // OpenGL ES 3.0 没有核心 GL_CLAMP_TO_BORDER。
    // 使用 CLAMP_TO_EDGE，并在 Shader 中手动处理越界坐标。
    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        GL_CLAMP_TO_EDGE
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        GL_CLAMP_TO_EDGE
    );

    glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_COMPARE_MODE,
        GL_NONE
    );

    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D,
        target.depthTexture,
        0
    );

    // 兼容性处理：给 FBO 附一个 dummy color renderbuffer。
    // Shadow pass 真正关心的是 depth attachment。
    glGenRenderbuffers(1, &target.dummyColorRenderbuffer);
    glBindRenderbuffer(
        GL_RENDERBUFFER,
        target.dummyColorRenderbuffer
    );

    glRenderbufferStorage(
        GL_RENDERBUFFER,
        GL_RGBA8,
        SHADOW_SIZE,
        SHADOW_SIZE
    );

    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_RENDERBUFFER,
        target.dummyColorRenderbuffer
    );

    GLenum status =
        glCheckFramebufferStatus(GL_FRAMEBUFFER);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(
            stderr,
            "Shadow framebuffer incomplete: 0x%04X\n",
            status
        );

        std::exit(EXIT_FAILURE);
    }

    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return target;
}

static Renderer createRenderer() {
    Renderer renderer;

    renderer.depthProgram = createDepthProgram();
    renderer.sceneProgram = createSceneProgram();

    renderer.cube = createCubeMesh();
    renderer.plane = createPlaneMesh();
    renderer.shadow = createShadowTarget();

    renderer.depthModelLocation =
        glGetUniformLocation(renderer.depthProgram, "uModel");

    renderer.depthLightViewProjectionLocation =
        glGetUniformLocation(
            renderer.depthProgram,
            "uLightViewProjection"
        );

    renderer.sceneModelLocation =
        glGetUniformLocation(renderer.sceneProgram, "uModel");

    renderer.sceneViewLocation =
        glGetUniformLocation(renderer.sceneProgram, "uView");

    renderer.sceneProjectionLocation =
        glGetUniformLocation(renderer.sceneProgram, "uProjection");

    renderer.sceneLightViewProjectionLocation =
        glGetUniformLocation(
            renderer.sceneProgram,
            "uLightViewProjection"
        );

    renderer.sceneNormalMatrixLocation =
        glGetUniformLocation(renderer.sceneProgram, "uNormalMatrix");

    renderer.sceneColorLocation =
        glGetUniformLocation(renderer.sceneProgram, "uColor");

    renderer.sceneLightDirectionLocation =
        glGetUniformLocation(renderer.sceneProgram, "uLightDirection");

    renderer.sceneShadowMapLocation =
        glGetUniformLocation(renderer.sceneProgram, "uShadowMap");

    glUseProgram(renderer.sceneProgram);

    glUniform1i(
        renderer.sceneShadowMapLocation,
        0
    );

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    return renderer;
}

static glm::mat4 cubeModelA(float elapsedSeconds) {
    glm::mat4 model(1.0f);

    model = glm::translate(
        model,
        glm::vec3(-0.9f, 0.0f, 0.0f)
    );

    model = glm::rotate(
        model,
        elapsedSeconds * 0.6f,
        glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f))
    );

    return model;
}

static glm::mat4 cubeModelB(float elapsedSeconds) {
    glm::mat4 model(1.0f);

    model = glm::translate(
        model,
        glm::vec3(0.9f, 0.15f, 0.45f)
    );

    model = glm::rotate(
        model,
        -elapsedSeconds * 0.8f,
        glm::normalize(glm::vec3(1.0f, 0.3f, 0.5f))
    );

    model = glm::scale(
        model,
        glm::vec3(0.75f)
    );

    return model;
}

static void drawDepthMesh(
    const Renderer& renderer,
    const Mesh& mesh,
    const glm::mat4& model,
    const glm::mat4& lightViewProjection
) {
    glUniformMatrix4fv(
        renderer.depthModelLocation,
        1,
        GL_FALSE,
        glm::value_ptr(model)
    );

    glUniformMatrix4fv(
        renderer.depthLightViewProjectionLocation,
        1,
        GL_FALSE,
        glm::value_ptr(lightViewProjection)
    );

    glBindVertexArray(mesh.vao);

    glDrawElements(
        GL_TRIANGLES,
        mesh.indexCount,
        GL_UNSIGNED_SHORT,
        nullptr
    );
}

static void renderShadowPass(
    const Renderer& renderer,
    const glm::mat4& lightViewProjection,
    float elapsedSeconds
) {
    glBindFramebuffer(
        GL_FRAMEBUFFER,
        renderer.shadow.framebuffer
    );

    glViewport(0, 0, SHADOW_SIZE, SHADOW_SIZE);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT
    );

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // 多边形偏移可以减少自阴影条纹。
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    glUseProgram(renderer.depthProgram);

    drawDepthMesh(
        renderer,
        renderer.plane,
        glm::mat4(1.0f),
        lightViewProjection
    );

    drawDepthMesh(
        renderer,
        renderer.cube,
        cubeModelA(elapsedSeconds),
        lightViewProjection
    );

    drawDepthMesh(
        renderer,
        renderer.cube,
        cubeModelB(elapsedSeconds),
        lightViewProjection
    );

    glDisable(GL_POLYGON_OFFSET_FILL);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void drawSceneMesh(
    const Renderer& renderer,
    const Mesh& mesh,
    const glm::mat4& model,
    const glm::vec3& color
) {
    glm::mat3 normalMatrix =
        glm::transpose(glm::inverse(glm::mat3(model)));

    glUniformMatrix4fv(
        renderer.sceneModelLocation,
        1,
        GL_FALSE,
        glm::value_ptr(model)
    );

    glUniformMatrix3fv(
        renderer.sceneNormalMatrixLocation,
        1,
        GL_FALSE,
        glm::value_ptr(normalMatrix)
    );

    glUniform3fv(
        renderer.sceneColorLocation,
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

static void renderScenePass(
    const Renderer& renderer,
    const glm::mat4& lightViewProjection,
    const glm::vec3& lightDirection,
    int width,
    int height,
    float elapsedSeconds
) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glViewport(0, 0, width, height);

    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT
    );

    glm::vec3 cameraPosition(
        0.0f,
        2.0f,
        5.2f
    );

    glm::mat4 view = glm::lookAt(
        cameraPosition,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    glm::mat4 projection = glm::perspective(
        glm::radians(55.0f),
        static_cast<float>(width) / static_cast<float>(height),
        0.1f,
        100.0f
    );

    glUseProgram(renderer.sceneProgram);

    glUniformMatrix4fv(
        renderer.sceneViewLocation,
        1,
        GL_FALSE,
        glm::value_ptr(view)
    );

    glUniformMatrix4fv(
        renderer.sceneProjectionLocation,
        1,
        GL_FALSE,
        glm::value_ptr(projection)
    );

    glUniformMatrix4fv(
        renderer.sceneLightViewProjectionLocation,
        1,
        GL_FALSE,
        glm::value_ptr(lightViewProjection)
    );

    glUniform3fv(
        renderer.sceneLightDirectionLocation,
        1,
        glm::value_ptr(lightDirection)
    );

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(
        GL_TEXTURE_2D,
        renderer.shadow.depthTexture
    );

    drawSceneMesh(
        renderer,
        renderer.plane,
        glm::mat4(1.0f),
        glm::vec3(0.72f, 0.72f, 0.68f)
    );

    drawSceneMesh(
        renderer,
        renderer.cube,
        cubeModelA(elapsedSeconds),
        glm::vec3(0.20f, 0.65f, 1.00f)
    );

    drawSceneMesh(
        renderer,
        renderer.cube,
        cubeModelB(elapsedSeconds),
        glm::vec3(1.00f, 0.42f, 0.25f)
    );
}

static void renderFrame(
    const Renderer& renderer,
    int width,
    int height,
    float elapsedSeconds
) {
    glm::vec3 lightDirection =
        glm::normalize(glm::vec3(-0.55f, -1.0f, -0.45f));

    glm::vec3 lightPosition =
        -lightDirection * 5.0f;

    glm::mat4 lightView = glm::lookAt(
        lightPosition,
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    // 方向光使用正交投影更直观。
    glm::mat4 lightProjection = glm::ortho(
        -4.5f,
        4.5f,
        -4.5f,
        4.5f,
        0.1f,
        12.0f
    );

    glm::mat4 lightViewProjection =
        lightProjection * lightView;

    renderShadowPass(
        renderer,
        lightViewProjection,
        elapsedSeconds
    );

    renderScenePass(
        renderer,
        lightViewProjection,
        lightDirection,
        width,
        height,
        elapsedSeconds
    );
}

static void destroyMesh(Mesh& mesh) {
    glDeleteBuffers(1, &mesh.ebo);
    glDeleteBuffers(1, &mesh.vbo);
    glDeleteVertexArrays(1, &mesh.vao);
}

static void destroyShadowTarget(ShadowTarget& target) {
    glDeleteRenderbuffers(
        1,
        &target.dummyColorRenderbuffer
    );

    glDeleteTextures(
        1,
        &target.depthTexture
    );

    glDeleteFramebuffers(
        1,
        &target.framebuffer
    );
}

static void destroyRenderer(Renderer& renderer) {
    destroyShadowTarget(renderer.shadow);

    destroyMesh(renderer.plane);
    destroyMesh(renderer.cube);

    glDeleteProgram(renderer.sceneProgram);
    glDeleteProgram(renderer.depthProgram);
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
        "25 - Shadow Map",
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
    std::printf("Shadow map size: %d x %d\n", SHADOW_SIZE, SHADOW_SIZE);

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