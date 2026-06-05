#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static bool gPrintAllExtensions = false;

static const char* safeString(const GLubyte* value) {
    return value ? reinterpret_cast<const char*>(value) : "(null)";
}

static const char* safeEglString(const char* value) {
    return value ? value : "(null)";
}

static void printHeader(const char* title) {
    std::printf("\n========== %s ==========\n", title);
}

static void printGlInteger(GLenum name, const char* label) {
    GLint value = 0;
    glGetIntegerv(name, &value);
    std::printf("%-42s : %d\n", label, value);
}

static void printGlInteger2(GLenum name, const char* label) {
    GLint values[2]{};
    glGetIntegerv(name, values);
    std::printf("%-42s : %d, %d\n", label, values[0], values[1]);
}

static std::vector<std::string> getGlExtensions() {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);

    std::vector<std::string> extensions;
    extensions.reserve(count);

    for (GLint i = 0; i < count; ++i) {
        const GLubyte* extension =
            glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));

        if (extension) {
            extensions.emplace_back(
                reinterpret_cast<const char*>(extension)
            );
        }
    }

    std::sort(extensions.begin(), extensions.end());
    return extensions;
}

static bool containsExtension(
    const std::vector<std::string>& extensions,
    const char* name
) {
    return std::find(
        extensions.begin(),
        extensions.end(),
        name
    ) != extensions.end();
}

static bool eglExtensionContains(
    const char* extensionString,
    const char* name
) {
    if (!extensionString) {
        return false;
    }

    std::string all = extensionString;
    std::string target = name;

    std::size_t position = all.find(target);

    while (position != std::string::npos) {
        bool leftOk =
            position == 0 ||
            all[position - 1] == ' ';

        std::size_t end = position + target.size();

        bool rightOk =
            end == all.size() ||
            all[end] == ' ';

        if (leftOk && rightOk) {
            return true;
        }

        position = all.find(target, position + 1);
    }

    return false;
}

static void printExtensionPresence(
    const std::vector<std::string>& extensions,
    const char* name
) {
    std::printf(
        "%-42s : %s\n",
        name,
        containsExtension(extensions, name) ? "YES" : "NO"
    );
}

static void printEglExtensionPresence(
    const char* extensionString,
    const char* name
) {
    std::printf(
        "%-42s : %s\n",
        name,
        eglExtensionContains(extensionString, name) ? "YES" : "NO"
    );
}

static void printEglInfo() {
    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();
    EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);

    printHeader("EGL");

    if (
        display == EGL_NO_DISPLAY ||
        context == EGL_NO_CONTEXT
        ) {
        std::printf("No current EGL context.\n");
        return;
    }

    std::printf(
        "%-42s : %s\n",
        "EGL_VERSION",
        safeEglString(eglQueryString(display, EGL_VERSION))
    );

    std::printf(
        "%-42s : %s\n",
        "EGL_VENDOR",
        safeEglString(eglQueryString(display, EGL_VENDOR))
    );

    std::printf(
        "%-42s : %s\n",
        "EGL_CLIENT_APIS",
        safeEglString(eglQueryString(display, EGL_CLIENT_APIS))
    );

    EGLint configId = 0;
    eglQueryContext(
        display,
        context,
        EGL_CONFIG_ID,
        &configId
    );

    std::printf("%-42s : %d\n", "EGL_CONFIG_ID", configId);

    if (surface != EGL_NO_SURFACE) {
        EGLint width = 0;
        EGLint height = 0;

        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);

        std::printf("%-42s : %d x %d\n", "EGL_SURFACE_SIZE", width, height);
    }

    const char* displayExtensions =
        eglQueryString(display, EGL_EXTENSIONS);

    const char* clientExtensions =
        eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);

    printHeader("EGL Extension Checks");

    printEglExtensionPresence(
        displayExtensions,
        "EGL_KHR_create_context"
    );

    printEglExtensionPresence(
        displayExtensions,
        "EGL_KHR_surfaceless_context"
    );

    printEglExtensionPresence(
        displayExtensions,
        "EGL_ANGLE_platform_angle"
    );

    printEglExtensionPresence(
        displayExtensions,
        "EGL_ANGLE_d3d_share_handle_client_buffer"
    );

    printEglExtensionPresence(
        clientExtensions,
        "EGL_EXT_platform_base"
    );

    printEglExtensionPresence(
        clientExtensions,
        "EGL_EXT_client_extensions"
    );

    if (gPrintAllExtensions) {
        printHeader("EGL Display Extensions");
        std::printf("%s\n", safeEglString(displayExtensions));

        printHeader("EGL Client Extensions");
        std::printf("%s\n", safeEglString(clientExtensions));
    }
}

static void printGlInfo() {
    printHeader("OpenGL ES Identity");

    std::printf(
        "%-42s : %s\n",
        "GL_VERSION",
        safeString(glGetString(GL_VERSION))
    );

    std::printf(
        "%-42s : %s\n",
        "GL_SHADING_LANGUAGE_VERSION",
        safeString(glGetString(GL_SHADING_LANGUAGE_VERSION))
    );

    std::printf(
        "%-42s : %s\n",
        "GL_VENDOR",
        safeString(glGetString(GL_VENDOR))
    );

    std::printf(
        "%-42s : %s\n",
        "GL_RENDERER",
        safeString(glGetString(GL_RENDERER))
    );
}

static void printTextureLimits() {
    printHeader("Texture Limits");

    printGlInteger(GL_MAX_TEXTURE_SIZE, "GL_MAX_TEXTURE_SIZE");
    printGlInteger(GL_MAX_CUBE_MAP_TEXTURE_SIZE, "GL_MAX_CUBE_MAP_TEXTURE_SIZE");
    printGlInteger(GL_MAX_3D_TEXTURE_SIZE, "GL_MAX_3D_TEXTURE_SIZE");
    printGlInteger(GL_MAX_ARRAY_TEXTURE_LAYERS, "GL_MAX_ARRAY_TEXTURE_LAYERS");

    printGlInteger(
        GL_MAX_TEXTURE_IMAGE_UNITS,
        "GL_MAX_TEXTURE_IMAGE_UNITS"
    );

    printGlInteger(
        GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,
        "GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS"
    );

    printGlInteger(
        GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
        "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS"
    );
}

static void printFramebufferLimits() {
    printHeader("Framebuffer Limits");

    printGlInteger(GL_MAX_RENDERBUFFER_SIZE, "GL_MAX_RENDERBUFFER_SIZE");
    printGlInteger(GL_MAX_DRAW_BUFFERS, "GL_MAX_DRAW_BUFFERS");
    printGlInteger(GL_MAX_COLOR_ATTACHMENTS, "GL_MAX_COLOR_ATTACHMENTS");
    printGlInteger(GL_MAX_SAMPLES, "GL_MAX_SAMPLES");
    printGlInteger2(GL_MAX_VIEWPORT_DIMS, "GL_MAX_VIEWPORT_DIMS");

    GLint sampleBuffers = 0;
    GLint samples = 0;

    glGetIntegerv(GL_SAMPLE_BUFFERS, &sampleBuffers);
    glGetIntegerv(GL_SAMPLES, &samples);

    std::printf("%-42s : %d\n", "Default GL_SAMPLE_BUFFERS", sampleBuffers);
    std::printf("%-42s : %d\n", "Default GL_SAMPLES", samples);
}

static void printVertexAndDrawLimits() {
    printHeader("Vertex And Draw Limits");

    printGlInteger(GL_MAX_VERTEX_ATTRIBS, "GL_MAX_VERTEX_ATTRIBS");
    printGlInteger(GL_MAX_ELEMENTS_VERTICES, "GL_MAX_ELEMENTS_VERTICES");
    printGlInteger(GL_MAX_ELEMENTS_INDICES, "GL_MAX_ELEMENTS_INDICES");

    printGlInteger(
        GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS,
        "GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS"
    );

    printGlInteger(
        GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS,
        "GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS"
    );

    printGlInteger(
        GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS,
        "GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS"
    );
}

static void printUniformLimits() {
    printHeader("Uniform And UBO Limits");

    printGlInteger(
        GL_MAX_VERTEX_UNIFORM_VECTORS,
        "GL_MAX_VERTEX_UNIFORM_VECTORS"
    );

    printGlInteger(
        GL_MAX_FRAGMENT_UNIFORM_VECTORS,
        "GL_MAX_FRAGMENT_UNIFORM_VECTORS"
    );

    printGlInteger(
        GL_MAX_UNIFORM_BUFFER_BINDINGS,
        "GL_MAX_UNIFORM_BUFFER_BINDINGS"
    );

    printGlInteger(
        GL_MAX_UNIFORM_BLOCK_SIZE,
        "GL_MAX_UNIFORM_BLOCK_SIZE"
    );

    printGlInteger(
        GL_MAX_VERTEX_UNIFORM_BLOCKS,
        "GL_MAX_VERTEX_UNIFORM_BLOCKS"
    );

    printGlInteger(
        GL_MAX_FRAGMENT_UNIFORM_BLOCKS,
        "GL_MAX_FRAGMENT_UNIFORM_BLOCKS"
    );

    printGlInteger(
        GL_MAX_COMBINED_UNIFORM_BLOCKS,
        "GL_MAX_COMBINED_UNIFORM_BLOCKS"
    );

    printGlInteger(
        GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,
        "GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT"
    );
}

static void printShaderPrecision() {
    printHeader("Shader Precision");

    GLint range[2]{};
    GLint precision = 0;

    glGetShaderPrecisionFormat(
        GL_FRAGMENT_SHADER,
        GL_LOW_FLOAT,
        range,
        &precision
    );

    std::printf(
        "%-42s : range %d..%d, precision %d\n",
        "Fragment lowp float",
        range[0],
        range[1],
        precision
    );

    glGetShaderPrecisionFormat(
        GL_FRAGMENT_SHADER,
        GL_MEDIUM_FLOAT,
        range,
        &precision
    );

    std::printf(
        "%-42s : range %d..%d, precision %d\n",
        "Fragment mediump float",
        range[0],
        range[1],
        precision
    );

    glGetShaderPrecisionFormat(
        GL_FRAGMENT_SHADER,
        GL_HIGH_FLOAT,
        range,
        &precision
    );

    std::printf(
        "%-42s : range %d..%d, precision %d\n",
        "Fragment highp float",
        range[0],
        range[1],
        precision
    );

    glGetShaderPrecisionFormat(
        GL_FRAGMENT_SHADER,
        GL_HIGH_INT,
        range,
        &precision
    );

    std::printf(
        "%-42s : range %d..%d, precision %d\n",
        "Fragment highp int",
        range[0],
        range[1],
        precision
    );
}

static void printInternalFormatInfo() {
    printHeader("Internal Format Info");

    const GLenum formats[] = {
        GL_RGBA8,
        GL_RGB8,
        GL_DEPTH_COMPONENT24,
        GL_DEPTH24_STENCIL8
    };

    const char* names[] = {
        "GL_RGBA8",
        "GL_RGB8",
        "GL_DEPTH_COMPONENT24",
        "GL_DEPTH24_STENCIL8"
    };

    for (int i = 0; i < 4; ++i) {
        GLint sampleCount = 0;

        glGetInternalformativ(
            GL_RENDERBUFFER,
            formats[i],
            GL_NUM_SAMPLE_COUNTS,
            1,
            &sampleCount
        );

        std::printf(
            "%-42s : sample count variants = %d",
            names[i],
            sampleCount
        );

        if (sampleCount > 0) {
            std::vector<GLint> samples(sampleCount);

            glGetInternalformativ(
                GL_RENDERBUFFER,
                formats[i],
                GL_SAMPLES,
                sampleCount,
                samples.data()
            );

            std::printf(", samples = ");

            for (int index = 0; index < sampleCount; ++index) {
                std::printf(
                    "%s%d",
                    index == 0 ? "" : ", ",
                    samples[index]
                );
            }
        }

        std::printf("\n");
    }
}

static void printExtensionSummary() {
    std::vector<std::string> extensions = getGlExtensions();

    printHeader("OpenGL ES Extension Summary");

    std::printf("%-42s : %zu\n", "GL extension count", extensions.size());

    printExtensionPresence(extensions, "GL_EXT_texture_filter_anisotropic");
    printExtensionPresence(extensions, "GL_EXT_color_buffer_float");
    printExtensionPresence(extensions, "GL_EXT_disjoint_timer_query");
    printExtensionPresence(extensions, "GL_EXT_debug_marker");
    printExtensionPresence(extensions, "GL_KHR_debug");
    printExtensionPresence(extensions, "GL_OES_texture_float_linear");
    printExtensionPresence(extensions, "GL_OES_EGL_image");
    printExtensionPresence(extensions, "GL_ANGLE_instanced_arrays");
    printExtensionPresence(extensions, "GL_ANGLE_multi_draw");

    if (gPrintAllExtensions) {
        printHeader("OpenGL ES Extensions");

        for (const std::string& extension : extensions) {
            std::printf("%s\n", extension.c_str());
        }
    }
}

static void printUsage(const char* executable) {
    std::printf("Usage: %s [--all-extensions]\n", executable);
}

static void parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--all-extensions") == 0) {
            gPrintAllExtensions = true;
            continue;
        }

        if (
            std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0
            ) {
            printUsage(argv[0]);
            std::exit(EXIT_SUCCESS);
        }

        std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
        printUsage(argv[0]);
        std::exit(EXIT_FAILURE);
    }
}

int main(int argc, char** argv) {
    parseArgs(argc, argv);

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // 这个工具不需要显示窗口。
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    GLFWwindow* window = glfwCreateWindow(
        640,
        480,
        "26 - Capabilities Report",
        nullptr,
        nullptr
    );

    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);

    printEglInfo();
    printGlInfo();
    printTextureLimits();
    printFramebufferLimits();
    printVertexAndDrawLimits();
    printUniformLimits();
    printShaderPrecision();
    printInternalFormatInfo();
    printExtensionSummary();

    GLenum error = glGetError();

    if (error != GL_NO_ERROR) {
        std::printf("\nGL error after report: 0x%04X\n", error);
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}