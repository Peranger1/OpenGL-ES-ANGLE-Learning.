#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

constexpr const char* CACHE_PATH = "32-program-binary-cache.bin";

struct CacheHeader {
    char magic[8];
    std::uint32_t version;
    std::uint32_t binaryFormat;
    std::uint32_t binarySize;
};

struct Renderer {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;

    GLint timeLocation = -1;
};

static const char* vertexSource = R"(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec3 aColor;

uniform mediump float uTime;

out vec3 vColor;

void main() {
    float angle = uTime * 0.8;
    float c = cos(angle);
    float s = sin(angle);

    mat2 rotation = mat2(
        c, s,
       -s, c
    );

    vec2 position = rotation * aPosition;

    vColor = aColor;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

static const char* fragmentSource = R"(#version 300 es
precision mediump float;

in vec3 vColor;

uniform mediump float uTime;

out vec4 fragColor;

void main() {
    float pulse = 0.75 + 0.25 * sin(uTime * 2.0);
    fragColor = vec4(vColor * pulse, 1.0);
}
)";

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

static bool checkProgramLink(GLuint program, const char* label) {
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);

    if (ok == GL_TRUE) {
        return true;
    }

    char log[2048]{};
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);

    std::fprintf(stderr, "%s failed:\n%s\n", label, log);
    return false;
}

static GLuint buildProgramFromSource() {
    GLuint vertexShader =
        compileShader(GL_VERTEX_SHADER, vertexSource, "vertex shader");

    GLuint fragmentShader =
        compileShader(GL_FRAGMENT_SHADER, fragmentSource, "fragment shader");

    GLuint program = glCreateProgram();

    // 必须在 glLinkProgram 之前设置。
    // 这告诉驱动：链接成功后，应用可能会读取 Program Binary。
    glProgramParameteri(
        program,
        GL_PROGRAM_BINARY_RETRIEVABLE_HINT,
        GL_TRUE
    );

    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    if (!checkProgramLink(program, "source program link")) {
        std::exit(EXIT_FAILURE);
    }

    return program;
}

static bool readFile(
    const char* path,
    std::vector<std::uint8_t>& data
) {
    FILE* file = nullptr;

#if defined(_MSC_VER)
    fopen_s(&file, path, "rb");
#else
    file = std::fopen(path, "rb");
#endif

    if (!file) {
        return false;
    }

    std::fseek(file, 0, SEEK_END);
    long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);

    if (size <= 0) {
        std::fclose(file);
        return false;
    }

    data.resize(static_cast<std::size_t>(size));

    std::size_t readCount =
        std::fread(data.data(), 1, data.size(), file);

    std::fclose(file);

    return readCount == data.size();
}

static bool writeFile(
    const char* path,
    const void* data,
    std::size_t size
) {
    FILE* file = nullptr;

#if defined(_MSC_VER)
    fopen_s(&file, path, "wb");
#else
    file = std::fopen(path, "wb");
#endif

    if (!file) {
        return false;
    }

    std::size_t written =
        std::fwrite(data, 1, size, file);

    std::fclose(file);

    return written == size;
}

static bool saveProgramBinary(GLuint program) {
    GLint binaryLength = 0;

    glGetProgramiv(
        program,
        GL_PROGRAM_BINARY_LENGTH,
        &binaryLength
    );

    if (binaryLength <= 0) {
        std::printf("Program binary is unavailable.\n");
        return false;
    }

    std::vector<std::uint8_t> binary(
        static_cast<std::size_t>(binaryLength)
    );

    GLsizei written = 0;
    GLenum binaryFormat = 0;

    glGetProgramBinary(
        program,
        binaryLength,
        &written,
        &binaryFormat,
        binary.data()
    );

    if (written <= 0) {
        std::printf("glGetProgramBinary returned empty data.\n");
        return false;
    }

    CacheHeader header{};
    std::memcpy(header.magic, "GLESBIN1", 8);
    header.version = 1;
    header.binaryFormat = binaryFormat;
    header.binarySize = static_cast<std::uint32_t>(written);

    std::vector<std::uint8_t> fileData(
        sizeof(CacheHeader) + static_cast<std::size_t>(written)
    );

    std::memcpy(
        fileData.data(),
        &header,
        sizeof(header)
    );

    std::memcpy(
        fileData.data() + sizeof(header),
        binary.data(),
        static_cast<std::size_t>(written)
    );

    if (!writeFile(CACHE_PATH, fileData.data(), fileData.size())) {
        std::printf("Failed to write cache file: %s\n", CACHE_PATH);
        return false;
    }

    std::printf(
        "Saved program binary: format=0x%04X, bytes=%d\n",
        binaryFormat,
        written
    );

    return true;
}

static GLuint tryLoadProgramBinary() {
    std::vector<std::uint8_t> fileData;

    if (!readFile(CACHE_PATH, fileData)) {
        std::printf("No program binary cache found.\n");
        return 0;
    }

    if (fileData.size() < sizeof(CacheHeader)) {
        std::printf("Program binary cache is too small.\n");
        return 0;
    }

    CacheHeader header{};
    std::memcpy(
        &header,
        fileData.data(),
        sizeof(header)
    );

    if (
        std::memcmp(header.magic, "GLESBIN1", 8) != 0 ||
        header.version != 1
        ) {
        std::printf("Program binary cache header mismatch.\n");
        return 0;
    }

    if (
        fileData.size() !=
        sizeof(CacheHeader) + static_cast<std::size_t>(header.binarySize)
        ) {
        std::printf("Program binary cache size mismatch.\n");
        return 0;
    }

    GLuint program = glCreateProgram();

    glProgramBinary(
        program,
        static_cast<GLenum>(header.binaryFormat),
        fileData.data() + sizeof(CacheHeader),
        static_cast<GLsizei>(header.binarySize)
    );

    if (!checkProgramLink(program, "program binary load")) {
        glDeleteProgram(program);
        return 0;
    }

    std::printf(
        "Loaded program binary: format=0x%04X, bytes=%u\n",
        header.binaryFormat,
        header.binarySize
    );

    return program;
}

static GLuint createCachedProgram() {
    GLint binaryFormats = 0;

    glGetIntegerv(
        GL_NUM_PROGRAM_BINARY_FORMATS,
        &binaryFormats
    );

    std::printf(
        "GL_NUM_PROGRAM_BINARY_FORMATS: %d\n",
        binaryFormats
    );

    if (binaryFormats <= 0) {
        std::printf("Program binary is not supported. Building from source.\n");
        return buildProgramFromSource();
    }

    GLuint program = tryLoadProgramBinary();

    if (program != 0) {
        return program;
    }

    std::printf("Building program from source.\n");

    program = buildProgramFromSource();
    saveProgramBinary(program);

    return program;
}

static Renderer createRenderer() {
    const float vertices[] = {
        // position       // color
         0.00f,  0.70f,   1.0f, 0.25f, 0.20f,
        -0.70f, -0.55f,   0.25f, 1.0f, 0.30f,
         0.70f, -0.55f,   0.25f, 0.45f, 1.0f
    };

    Renderer renderer;

    renderer.program = createCachedProgram();
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

    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        nullptr
    );
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        reinterpret_cast<void*>(2 * sizeof(float))
    );
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    return renderer;
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

    glUseProgram(renderer.program);

    glUniform1f(
        renderer.timeLocation,
        elapsedSeconds
    );

    glBindVertexArray(renderer.vao);

    glDrawArrays(
        GL_TRIANGLES,
        0,
        3
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
        900,
        650,
        "32 - Program Binary Cache",
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
    std::printf("Cache file  : %s\n", CACHE_PATH);

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