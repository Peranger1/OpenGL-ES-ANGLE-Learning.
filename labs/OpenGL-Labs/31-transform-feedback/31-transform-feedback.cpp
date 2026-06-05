#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

constexpr int PARTICLE_COUNT = 2000;

struct Particle {
    float position[2];
    float velocity[2];
};

struct Renderer {
    GLuint updateProgram = 0;
    GLuint renderProgram = 0;

    GLuint buffers[2]{};
    GLuint vaos[2]{};
    GLuint transformFeedback = 0;

    GLint deltaTimeLocation = -1;
    GLint pointSizeLocation = -1;

    int currentBuffer = 0;
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

static GLuint createUpdateProgram() {
    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aVelocity;

uniform float uDeltaTime;

// 这两个 out 不进入 Fragment Shader。
// 它们会被 Transform Feedback 捕获到 Buffer 中。
out vec2 vNextPosition;
out vec2 vNextVelocity;

void main() {
    vec2 position = aPosition;
    vec2 velocity = aVelocity;

    vec2 gravity = vec2(0.0, -0.35);

    velocity += gravity * uDeltaTime;
    position += velocity * uDeltaTime;

    // 简单边界反弹。
    if (position.x < -0.98) {
        position.x = -0.98;
        velocity.x = abs(velocity.x) * 0.92;
    }

    if (position.x > 0.98) {
        position.x = 0.98;
        velocity.x = -abs(velocity.x) * 0.92;
    }

    if (position.y < -0.98) {
        position.y = -0.98;
        velocity.y = abs(velocity.y) * 0.86;
    }

    if (position.y > 0.98) {
        position.y = 0.98;
        velocity.y = -abs(velocity.y) * 0.92;
    }

    vNextPosition = position;
    vNextVelocity = velocity;

    // Vertex Shader 仍然必须写 gl_Position。
    // 但更新 pass 会开启 GL_RASTERIZER_DISCARD，不会真正光栅化。
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

out vec4 fragColor;

void main() {
    fragColor = vec4(0.0);
}
)";

    GLuint vertexShader =
        compileShader(GL_VERTEX_SHADER, vertexSource, "update vertex shader");

    GLuint fragmentShader =
        compileShader(GL_FRAGMENT_SHADER, fragmentSource, "update fragment shader");

    GLuint program = glCreateProgram();

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);

    // 必须在 glLinkProgram 之前声明要捕获哪些 Vertex Shader 输出。
    const GLchar* varyings[] = {
        "vNextPosition",
        "vNextVelocity"
    };

    glTransformFeedbackVaryings(
        program,
        2,
        varyings,
        GL_INTERLEAVED_ATTRIBS
    );

    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);

    if (!ok) {
        char log[2048]{};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "update program link failed:\n%s\n", log);
        std::exit(EXIT_FAILURE);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

static GLuint createRenderProgram() {
    const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aVelocity;

uniform float uPointSize;

out vec3 vColor;

void main() {
    float speed = length(aVelocity);

    vColor = mix(
        vec3(0.20, 0.55, 1.00),
        vec3(1.00, 0.35, 0.15),
        clamp(speed * 1.6, 0.0, 1.0)
    );

    gl_PointSize = uPointSize;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

    const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec3 vColor;
out vec4 fragColor;

void main() {
    // 让点看起来像圆形粒子。
    vec2 p = gl_PointCoord * 2.0 - 1.0;
    float d = dot(p, p);

    if (d > 1.0) {
        discard;
    }

    float alpha = 1.0 - smoothstep(0.65, 1.0, d);
    fragColor = vec4(vColor, alpha);
}
)";

    GLuint vertexShader =
        compileShader(GL_VERTEX_SHADER, vertexSource, "render vertex shader");

    GLuint fragmentShader =
        compileShader(GL_FRAGMENT_SHADER, fragmentSource, "render fragment shader");

    GLuint program = glCreateProgram();

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);

    if (!ok) {
        char log[2048]{};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "render program link failed:\n%s\n", log);
        std::exit(EXIT_FAILURE);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

static std::vector<Particle> createInitialParticles() {
    std::vector<Particle> particles(PARTICLE_COUNT);

    for (int i = 0; i < PARTICLE_COUNT; ++i) {
        float t =
            static_cast<float>(i) /
            static_cast<float>(PARTICLE_COUNT);

        float angle = t * 6.2831853f * 16.0f;
        float radius = 0.05f + 0.18f * std::fmod(t * 13.0f, 1.0f);

        particles[i].position[0] = std::cos(angle) * radius;
        particles[i].position[1] = std::sin(angle) * radius + 0.35f;

        particles[i].velocity[0] = std::cos(angle) * (0.25f + t * 0.35f);
        particles[i].velocity[1] = std::sin(angle) * (0.25f + t * 0.35f);
    }

    return particles;
}

static void configureParticleVao(GLuint vao, GLuint buffer) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);

    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Particle),
        reinterpret_cast<void*>(offsetof(Particle, position))
    );
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Particle),
        reinterpret_cast<void*>(offsetof(Particle, velocity))
    );
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

static Renderer createRenderer() {
    Renderer renderer;

    renderer.updateProgram = createUpdateProgram();
    renderer.renderProgram = createRenderProgram();

    renderer.deltaTimeLocation =
        glGetUniformLocation(renderer.updateProgram, "uDeltaTime");

    renderer.pointSizeLocation =
        glGetUniformLocation(renderer.renderProgram, "uPointSize");

    std::vector<Particle> initialParticles =
        createInitialParticles();

    glGenBuffers(2, renderer.buffers);
    glGenVertexArrays(2, renderer.vaos);

    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_ARRAY_BUFFER, renderer.buffers[i]);

        glBufferData(
            GL_ARRAY_BUFFER,
            initialParticles.size() * sizeof(Particle),
            initialParticles.data(),
            GL_DYNAMIC_COPY
        );

        configureParticleVao(
            renderer.vaos[i],
            renderer.buffers[i]
        );
    }

    glGenTransformFeedbacks(
        1,
        &renderer.transformFeedback
    );

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    return renderer;
}

static void updateParticles(
    Renderer& renderer,
    float deltaTime
) {
    int sourceIndex = renderer.currentBuffer;
    int destinationIndex = 1 - renderer.currentBuffer;

    glUseProgram(renderer.updateProgram);

    glUniform1f(
        renderer.deltaTimeLocation,
        deltaTime
    );

    // Transform Feedback 更新 pass 不需要光栅化。
    glEnable(GL_RASTERIZER_DISCARD);

    glBindVertexArray(renderer.vaos[sourceIndex]);

    glBindTransformFeedback(
        GL_TRANSFORM_FEEDBACK,
        renderer.transformFeedback
    );

    // 将 Vertex Shader 的 out 写入 destination buffer。
    glBindBufferBase(
        GL_TRANSFORM_FEEDBACK_BUFFER,
        0,
        renderer.buffers[destinationIndex]
    );

    glBeginTransformFeedback(GL_POINTS);

    // 注意：Transform Feedback active 时，DrawArrays 的 primitive mode
    // 必须与 glBeginTransformFeedback 的 mode 匹配。
    glDrawArrays(
        GL_POINTS,
        0,
        PARTICLE_COUNT
    );

    glEndTransformFeedback();

    glBindBufferBase(
        GL_TRANSFORM_FEEDBACK_BUFFER,
        0,
        0
    );

    glBindTransformFeedback(
        GL_TRANSFORM_FEEDBACK,
        0
    );

    glDisable(GL_RASTERIZER_DISCARD);

    renderer.currentBuffer = destinationIndex;
}

static void renderParticles(const Renderer& renderer) {
    glUseProgram(renderer.renderProgram);

    glUniform1f(
        renderer.pointSizeLocation,
        5.0f
    );

    glBindVertexArray(
        renderer.vaos[renderer.currentBuffer]
    );

    glDrawArrays(
        GL_POINTS,
        0,
        PARTICLE_COUNT
    );
}

static void renderFrame(
    Renderer& renderer,
    int width,
    int height,
    float deltaTime
) {
    updateParticles(renderer, deltaTime);

    glViewport(0, 0, width, height);

    glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    renderParticles(renderer);
}

static void destroyRenderer(Renderer& renderer) {
    glDeleteTransformFeedbacks(
        1,
        &renderer.transformFeedback
    );

    glDeleteVertexArrays(2, renderer.vaos);
    glDeleteBuffers(2, renderer.buffers);

    glDeleteProgram(renderer.renderProgram);
    glDeleteProgram(renderer.updateProgram);
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
        1100,
        760,
        "31 - Transform Feedback",
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

    GLint maxInterleavedComponents = 0;
    glGetIntegerv(
        GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS,
        &maxInterleavedComponents
    );

    std::printf(
        "GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS: %d\n",
        maxInterleavedComponents
    );

    Renderer renderer = createRenderer();

    double previousTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        double currentTime = glfwGetTime();
        float deltaTime =
            static_cast<float>(currentTime - previousTime);

        previousTime = currentTime;

        // 防止调试断点后 deltaTime 过大，粒子飞出场景。
        if (deltaTime > 0.033f) {
            deltaTime = 0.033f;
        }

        int width = 0;
        int height = 0;

        glfwGetFramebufferSize(window, &width, &height);

        if (width > 0 && height > 0) {
            renderFrame(
                renderer,
                width,
                height,
                deltaTime
            );

            glfwSwapBuffers(window);
        }
    }

    destroyRenderer(renderer);

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}