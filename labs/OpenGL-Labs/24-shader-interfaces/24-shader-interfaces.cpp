#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

struct Vertex {
    float position[2];
    float smoothColor[3];
    float flatColor[3];
    float uv[2];
    int triangleId;
};

struct Renderer {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;

    GLint offsetLocation = -1;
    GLint scaleLocation = -1;
    GLint modeLocation = -1;
    GLint timeLocation = -1;
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

static GLuint createProgram() {
    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec3 aSmoothColor;
layout(location = 2) in vec3 aFlatColor;
layout(location = 3) in vec2 aUv;
layout(location = 4) in int aTriangleId;

uniform vec2 uOffset;
uniform vec2 uScale;

// smooth 是默认插值方式。
// 这里显式写出来，是为了观察接口语义。
smooth out vec3 vSmoothColor;

// flat 表示不插值。
// 三角形内部所有片元使用同一个值。
flat out vec3 vFlatColor;

// 没写修饰符时，默认也是 smooth。
out vec2 vUv;

// GLSL ES 3.00 要求整数 varying 必须使用 flat。
flat out int vTriangleId;

void main() {
    vSmoothColor = aSmoothColor;
    vFlatColor = aFlatColor;
    vUv = aUv;
    vTriangleId = aTriangleId;

    vec2 position = aPosition * uScale + uOffset;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;
precision highp int;

// Fragment Shader 的输入必须与 Vertex Shader 的输出匹配：
// 名字、类型和插值修饰符都要兼容。
smooth in vec3 vSmoothColor;
flat in vec3 vFlatColor;
in vec2 vUv;
flat in int vTriangleId;

uniform int uMode;
uniform float uTime;

out vec4 fragColor;

void main() {
    if (uMode == 0) {
        // smooth 插值：
        // 三角形三个顶点的颜色会在片元之间平滑过渡。
        fragColor = vec4(vSmoothColor, 1.0);
        return;
    }

    if (uMode == 1) {
        // flat 不插值：
        // 每个三角形内部颜色保持一致。
        //
        // vTriangleId 也是 flat int。
        // 如果把 Fragment Shader 里的 flat 去掉，GLSL ES 3.00 会报错。
        vec3 idTint =
            vTriangleId == 0
                ? vec3(1.0, 0.85, 0.25)
                : vec3(0.25, 0.65, 1.0);

        fragColor = vec4(vFlatColor * idTint, 1.0);
        return;
    }

    if (uMode == 2) {
        // discard：
        // 当前片元被丢弃，不会写入颜色缓冲区。
        //
        // 这里用 UV 做一个动态圆点镂空效果。
        vec2 cell = fract(vUv * 8.0 + vec2(uTime * 0.35, 0.0));
        float distanceToCenter = length(cell - vec2(0.5));

        if (distanceToCenter < 0.23) {
            discard;
        }

        fragColor = vec4(vSmoothColor, 1.0);
        return;
    }

    // precision 观察：
    // highp / mediump / lowp 是变量存储和计算精度提示。
    // 桌面 ANGLE 上通常不容易看出明显差异，但移动 GPU 上更重要。
    highp float highFrequency =
        fract((vUv.x + vUv.y) * 48.0 + uTime * 0.5);

    mediump float band =
        highFrequency < 0.5 ? 0.25 : 1.0;

    vec3 color =
        mix(vFlatColor, vSmoothColor, 0.45) * band;

    fragColor = vec4(color, 1.0);
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

static Renderer createRenderer() {
    // 两个三角形组成一个 Quad。
    // 注意这里没有复用顶点，因为每个三角形需要稳定的 flatColor 和 triangleId。
    const Vertex vertices[] = {
        // triangle 0
        {{-1.0f, -1.0f}, {1.0f, 0.1f, 0.1f}, {1.0f, 0.35f, 0.25f}, {0.0f, 0.0f}, 0},
        {{ 1.0f, -1.0f}, {0.1f, 1.0f, 0.1f}, {1.0f, 0.35f, 0.25f}, {1.0f, 0.0f}, 0},
        {{ 1.0f,  1.0f}, {0.1f, 0.3f, 1.0f}, {1.0f, 0.35f, 0.25f}, {1.0f, 1.0f}, 0},

        // triangle 1
        {{ 1.0f,  1.0f}, {0.1f, 0.3f, 1.0f}, {0.25f, 0.55f, 1.0f}, {1.0f, 1.0f}, 1},
        {{-1.0f,  1.0f}, {1.0f, 1.0f, 0.1f}, {0.25f, 0.55f, 1.0f}, {0.0f, 1.0f}, 1},
        {{-1.0f, -1.0f}, {1.0f, 0.1f, 0.1f}, {0.25f, 0.55f, 1.0f}, {0.0f, 0.0f}, 1}
    };

    Renderer renderer;
    renderer.program = createProgram();

    renderer.offsetLocation =
        glGetUniformLocation(renderer.program, "uOffset");

    renderer.scaleLocation =
        glGetUniformLocation(renderer.program, "uScale");

    renderer.modeLocation =
        glGetUniformLocation(renderer.program, "uMode");

    renderer.timeLocation =
        glGetUniformLocation(renderer.program, "uTime");

    glGenVertexArrays(1, &renderer.vao);
    glGenBuffers(1, &renderer.vbo);

    glBindVertexArray(renderer.vao);

    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo);

    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(vertices),
        vertices,
        GL_STATIC_DRAW
    );

    // location 0: vec2 position
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, position))
    );
    glEnableVertexAttribArray(0);

    // location 1: vec3 smooth color
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, smoothColor))
    );
    glEnableVertexAttribArray(1);

    // location 2: vec3 flat color
    glVertexAttribPointer(
        2,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, flatColor))
    );
    glEnableVertexAttribArray(2);

    // location 3: vec2 uv
    glVertexAttribPointer(
        3,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, uv))
    );
    glEnableVertexAttribArray(3);

    // location 4: int triangle id
    //
    // 整数 attribute 必须使用 glVertexAttribIPointer。
    // 如果用 glVertexAttribPointer，会走浮点转换路径。
    glVertexAttribIPointer(
        4,
        1,
        GL_INT,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, triangleId))
    );
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return renderer;
}

static void drawPanel(
    const Renderer& renderer,
    int mode,
    float offsetX,
    float offsetY,
    float scaleX,
    float scaleY,
    float elapsedSeconds
) {
    glUseProgram(renderer.program);

    glUniform2f(
        renderer.offsetLocation,
        offsetX,
        offsetY
    );

    glUniform2f(
        renderer.scaleLocation,
        scaleX,
        scaleY
    );

    glUniform1i(
        renderer.modeLocation,
        mode
    );

    glUniform1f(
        renderer.timeLocation,
        elapsedSeconds
    );

    glBindVertexArray(renderer.vao);

    glDrawArrays(
        GL_TRIANGLES,
        0,
        6
    );
}

static void renderFrame(
    const Renderer& renderer,
    int width,
    int height,
    float elapsedSeconds
) {
    glViewport(0, 0, width, height);

    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 左上：smooth interpolation
    drawPanel(
        renderer,
        0,
        -0.52f,
        0.52f,
        0.42f,
        0.38f,
        elapsedSeconds
    );

    // 右上：flat interpolation + flat int
    drawPanel(
        renderer,
        1,
        0.52f,
        0.52f,
        0.42f,
        0.38f,
        elapsedSeconds
    );

    // 左下：discard
    drawPanel(
        renderer,
        2,
        -0.52f,
        -0.52f,
        0.42f,
        0.38f,
        elapsedSeconds
    );

    // 右下：precision / 高频条纹
    drawPanel(
        renderer,
        3,
        0.52f,
        -0.52f,
        0.42f,
        0.38f,
        elapsedSeconds
    );
}

static void destroyRenderer(Renderer& renderer) {
    glDeleteBuffers(1, &renderer.vbo);
    glDeleteVertexArrays(1, &renderer.vao);
    glDeleteProgram(renderer.program);
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

    GLFWwindow* window = glfwCreateWindow(
        1000,
        720,
        "24 - Shader Interfaces",
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

    std::printf("\nPanels:\n");
    std::printf("  top-left     : smooth interpolation\n");
    std::printf("  top-right    : flat interpolation + flat int varying\n");
    std::printf("  bottom-left  : discard\n");
    std::printf("  bottom-right : precision / high-frequency bands\n");
    std::printf("\nPress ESC to exit.\n");

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