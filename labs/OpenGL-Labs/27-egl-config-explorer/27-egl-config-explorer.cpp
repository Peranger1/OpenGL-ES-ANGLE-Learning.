#include <EGL/egl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

struct Options {
    bool printAll = false;
    bool requireWindow = true;
    bool requirePbuffer = false;
    bool requireEs3 = true;
    bool requireDepth = false;
    bool requireStencil = false;
    bool requireMsaa = false;
};

struct ConfigInfo {
    EGLConfig config = nullptr;

    EGLint id = 0;
    EGLint red = 0;
    EGLint green = 0;
    EGLint blue = 0;
    EGLint alpha = 0;
    EGLint depth = 0;
    EGLint stencil = 0;

    EGLint surfaceType = 0;
    EGLint renderableType = 0;
    EGLint conformant = 0;
    EGLint caveat = 0;

    EGLint sampleBuffers = 0;
    EGLint samples = 0;

    EGLint nativeVisualId = 0;
    EGLint nativeVisualType = 0;

    EGLint bindToTextureRgb = 0;
    EGLint bindToTextureRgba = 0;

    EGLint maxPbufferWidth = 0;
    EGLint maxPbufferHeight = 0;
    EGLint maxPbufferPixels = 0;
};

static Options gOptions;

static void printUsage(const char* executable) {
    std::printf(
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --all        Print all EGLConfigs\n"
        "  --window     Require EGL_WINDOW_BIT, default ON\n"
        "  --pbuffer    Require EGL_PBUFFER_BIT\n"
        "  --es3        Require EGL_OPENGL_ES3_BIT, default ON\n"
        "  --depth      Require depth buffer\n"
        "  --stencil    Require stencil buffer\n"
        "  --msaa       Require sample buffers\n"
        "  --help       Show help\n",
        executable
    );
}

static void parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--all") == 0) {
            gOptions.printAll = true;
            gOptions.requireWindow = false;
            gOptions.requireEs3 = false;
            continue;
        }

        if (std::strcmp(argv[i], "--window") == 0) {
            gOptions.requireWindow = true;
            continue;
        }

        if (std::strcmp(argv[i], "--pbuffer") == 0) {
            gOptions.requirePbuffer = true;
            continue;
        }

        if (std::strcmp(argv[i], "--es3") == 0) {
            gOptions.requireEs3 = true;
            continue;
        }

        if (std::strcmp(argv[i], "--depth") == 0) {
            gOptions.requireDepth = true;
            continue;
        }

        if (std::strcmp(argv[i], "--stencil") == 0) {
            gOptions.requireStencil = true;
            continue;
        }

        if (std::strcmp(argv[i], "--msaa") == 0) {
            gOptions.requireMsaa = true;
            continue;
        }

        if (
            std::strcmp(argv[i], "--help") == 0 ||
            std::strcmp(argv[i], "-h") == 0
            ) {
            printUsage(argv[0]);
            std::exit(EXIT_SUCCESS);
        }

        std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
        printUsage(argv[0]);
        std::exit(EXIT_FAILURE);
    }
}

static const char* safeString(const char* value) {
    return value ? value : "(null)";
}

static void checkEgl(EGLBoolean ok, const char* operation) {
    if (ok != EGL_TRUE) {
        std::fprintf(
            stderr,
            "%s failed, EGL error: 0x%04X\n",
            operation,
            eglGetError()
        );

        std::exit(EXIT_FAILURE);
    }
}

static EGLint getConfigAttrib(
    EGLDisplay display,
    EGLConfig config,
    EGLint attribute
) {
    EGLint value = 0;

    if (
        eglGetConfigAttrib(
            display,
            config,
            attribute,
            &value
        ) != EGL_TRUE
        ) {
        return 0;
    }

    return value;
}

static bool hasBit(EGLint value, EGLint bit) {
    return (value & bit) == bit;
}

static std::string surfaceFlags(EGLint value) {
    std::string result;

    if (hasBit(value, EGL_WINDOW_BIT)) {
        result += "W";
    }

    if (hasBit(value, EGL_PBUFFER_BIT)) {
        result += "P";
    }

    if (hasBit(value, EGL_PIXMAP_BIT)) {
        result += "X";
    }

    return result.empty() ? "-" : result;
}

static std::string renderableFlags(EGLint value) {
    std::string result;

    if (hasBit(value, EGL_OPENGL_ES_BIT)) {
        result += "ES1 ";
    }

    if (hasBit(value, EGL_OPENGL_ES2_BIT)) {
        result += "ES2 ";
    }

#ifdef EGL_OPENGL_ES3_BIT
    if (hasBit(value, EGL_OPENGL_ES3_BIT)) {
        result += "ES3 ";
    }
#endif

    if (hasBit(value, EGL_OPENGL_BIT)) {
        result += "GL ";
    }

    if (hasBit(value, EGL_OPENVG_BIT)) {
        result += "VG ";
    }

    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }

    return result.empty() ? "-" : result;
}

static const char* caveatName(EGLint caveat) {
    switch (caveat) {
    case EGL_NONE:
        return "none";

    case EGL_SLOW_CONFIG:
        return "slow";

    case EGL_NON_CONFORMANT_CONFIG:
        return "nonconf";

    default:
        return "unknown";
    }
}

static ConfigInfo queryConfig(
    EGLDisplay display,
    EGLConfig config
) {
    ConfigInfo info;
    info.config = config;

    info.id = getConfigAttrib(display, config, EGL_CONFIG_ID);

    info.red = getConfigAttrib(display, config, EGL_RED_SIZE);
    info.green = getConfigAttrib(display, config, EGL_GREEN_SIZE);
    info.blue = getConfigAttrib(display, config, EGL_BLUE_SIZE);
    info.alpha = getConfigAttrib(display, config, EGL_ALPHA_SIZE);
    info.depth = getConfigAttrib(display, config, EGL_DEPTH_SIZE);
    info.stencil = getConfigAttrib(display, config, EGL_STENCIL_SIZE);

    info.surfaceType =
        getConfigAttrib(display, config, EGL_SURFACE_TYPE);

    info.renderableType =
        getConfigAttrib(display, config, EGL_RENDERABLE_TYPE);

    info.conformant =
        getConfigAttrib(display, config, EGL_CONFORMANT);

    info.caveat =
        getConfigAttrib(display, config, EGL_CONFIG_CAVEAT);

    info.sampleBuffers =
        getConfigAttrib(display, config, EGL_SAMPLE_BUFFERS);

    info.samples =
        getConfigAttrib(display, config, EGL_SAMPLES);

    info.nativeVisualId =
        getConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID);

    info.nativeVisualType =
        getConfigAttrib(display, config, EGL_NATIVE_VISUAL_TYPE);

    info.bindToTextureRgb =
        getConfigAttrib(display, config, EGL_BIND_TO_TEXTURE_RGB);

    info.bindToTextureRgba =
        getConfigAttrib(display, config, EGL_BIND_TO_TEXTURE_RGBA);

    info.maxPbufferWidth =
        getConfigAttrib(display, config, EGL_MAX_PBUFFER_WIDTH);

    info.maxPbufferHeight =
        getConfigAttrib(display, config, EGL_MAX_PBUFFER_HEIGHT);

    info.maxPbufferPixels =
        getConfigAttrib(display, config, EGL_MAX_PBUFFER_PIXELS);

    return info;
}

static bool matchesOptions(const ConfigInfo& info) {
    if (gOptions.printAll) {
        return true;
    }

    if (
        gOptions.requireWindow &&
        !hasBit(info.surfaceType, EGL_WINDOW_BIT)
        ) {
        return false;
    }

    if (
        gOptions.requirePbuffer &&
        !hasBit(info.surfaceType, EGL_PBUFFER_BIT)
        ) {
        return false;
    }

#ifdef EGL_OPENGL_ES3_BIT
    if (
        gOptions.requireEs3 &&
        !hasBit(info.renderableType, EGL_OPENGL_ES3_BIT)
        ) {
        return false;
    }
#endif

    if (gOptions.requireDepth && info.depth <= 0) {
        return false;
    }

    if (gOptions.requireStencil && info.stencil <= 0) {
        return false;
    }

    if (
        gOptions.requireMsaa &&
        (info.sampleBuffers <= 0 || info.samples <= 1)
        ) {
        return false;
    }

    return true;
}

static void printConfigTable(
    const std::vector<ConfigInfo>& configs
) {
    std::printf(
        "\n%-4s %-4s %-12s %-8s %-5s %-5s %-5s %-5s "
        "%-5s %-7s %-7s %-9s %-7s\n",
        "ID",
        "Surf",
        "API",
        "RGBA",
        "D",
        "S",
        "SB",
        "Samp",
        "Cnf",
        "Caveat",
        "Visual",
        "BindTex",
        "Pbuf"
    );

    std::printf(
        "%-4s %-4s %-12s %-8s %-5s %-5s %-5s %-5s "
        "%-5s %-7s %-7s %-9s %-7s\n",
        "----",
        "----",
        "------------",
        "--------",
        "-----",
        "-----",
        "-----",
        "-----",
        "-----",
        "-------",
        "-------",
        "---------",
        "-------"
    );

    for (const ConfigInfo& info : configs) {
        char rgba[32]{};
        std::snprintf(
            rgba,
            sizeof(rgba),
            "%d%d%d%d",
            info.red,
            info.green,
            info.blue,
            info.alpha
        );

        char bindTex[32]{};
        std::snprintf(
            bindTex,
            sizeof(bindTex),
            "%s%s",
            info.bindToTextureRgb == EGL_TRUE ? "RGB" : "",
            info.bindToTextureRgba == EGL_TRUE ? "RGBA" : ""
        );

        if (bindTex[0] == '\0') {
            std::snprintf(bindTex, sizeof(bindTex), "-");
        }

        char pbuffer[32]{};
        if (hasBit(info.surfaceType, EGL_PBUFFER_BIT)) {
            std::snprintf(
                pbuffer,
                sizeof(pbuffer),
                "%dx%d",
                info.maxPbufferWidth,
                info.maxPbufferHeight
            );
        }
        else {
            std::snprintf(pbuffer, sizeof(pbuffer), "-");
        }

        std::printf(
            "%-4d %-4s %-12s %-8s %-5d %-5d %-5d %-5d "
            "%-5s %-7s 0x%-5X %-9s %-7s\n",
            info.id,
            surfaceFlags(info.surfaceType).c_str(),
            renderableFlags(info.renderableType).c_str(),
            rgba,
            info.depth,
            info.stencil,
            info.sampleBuffers,
            info.samples,
            hasBit(info.conformant, EGL_OPENGL_ES2_BIT) ? "yes" : "-",
            caveatName(info.caveat),
            info.nativeVisualId,
            bindTex,
            pbuffer
        );
    }
}

static void printSummary(
    const std::vector<ConfigInfo>& configs
) {
    int windowCount = 0;
    int pbufferCount = 0;
    int es2Count = 0;
    int es3Count = 0;
    int depthCount = 0;
    int stencilCount = 0;
    int msaaCount = 0;

    for (const ConfigInfo& info : configs) {
        if (hasBit(info.surfaceType, EGL_WINDOW_BIT)) {
            ++windowCount;
        }

        if (hasBit(info.surfaceType, EGL_PBUFFER_BIT)) {
            ++pbufferCount;
        }

        if (hasBit(info.renderableType, EGL_OPENGL_ES2_BIT)) {
            ++es2Count;
        }

#ifdef EGL_OPENGL_ES3_BIT
        if (hasBit(info.renderableType, EGL_OPENGL_ES3_BIT)) {
            ++es3Count;
        }
#endif

        if (info.depth > 0) {
            ++depthCount;
        }

        if (info.stencil > 0) {
            ++stencilCount;
        }

        if (info.sampleBuffers > 0 && info.samples > 1) {
            ++msaaCount;
        }
    }

    std::printf("\nSummary:\n");
    std::printf("  total configs     : %zu\n", configs.size());
    std::printf("  window configs    : %d\n", windowCount);
    std::printf("  pbuffer configs   : %d\n", pbufferCount);
    std::printf("  ES2 configs       : %d\n", es2Count);
    std::printf("  ES3 configs       : %d\n", es3Count);
    std::printf("  depth configs     : %d\n", depthCount);
    std::printf("  stencil configs   : %d\n", stencilCount);
    std::printf("  MSAA configs      : %d\n", msaaCount);
}

static void printChooseConfigResult(
    EGLDisplay display,
    const char* name,
    const EGLint* attributes
) {
    EGLConfig configs[16]{};
    EGLint count = 0;

    EGLBoolean ok =
        eglChooseConfig(
            display,
            attributes,
            configs,
            16,
            &count
        );

    std::printf("\nChooseConfig: %s\n", name);

    if (ok != EGL_TRUE) {
        std::printf(
            "  failed, EGL error: 0x%04X\n",
            eglGetError()
        );

        return;
    }

    std::printf("  matches: %d\n", count);

    if (count > 0) {
        ConfigInfo first =
            queryConfig(display, configs[0]);

        std::printf(
            "  first config id=%d, RGBA=%d%d%d%d, D=%d, S=%d, samples=%d\n",
            first.id,
            first.red,
            first.green,
            first.blue,
            first.alpha,
            first.depth,
            first.stencil,
            first.samples
        );
    }
}

static void runChooseConfigExamples(EGLDisplay display) {
    const EGLint windowEs3DepthStencil[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };

    const EGLint windowEs3Msaa4[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_SAMPLE_BUFFERS, 1,
        EGL_SAMPLES, 4,
        EGL_NONE
    };

    const EGLint pbufferEs3[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    printChooseConfigResult(
        display,
        "window + ES3 + RGBA8 + depth24 + stencil8",
        windowEs3DepthStencil
    );

    printChooseConfigResult(
        display,
        "window + ES3 + RGBA8 + MSAA 4x",
        windowEs3Msaa4
    );

    printChooseConfigResult(
        display,
        "pbuffer + ES3 + RGBA8",
        pbufferEs3
    );
}

int main(int argc, char** argv) {
    parseArgs(argc, argv);

    EGLDisplay display =
        eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if (display == EGL_NO_DISPLAY) {
        std::fprintf(
            stderr,
            "eglGetDisplay failed, EGL error: 0x%04X\n",
            eglGetError()
        );

        return EXIT_FAILURE;
    }

    EGLint major = 0;
    EGLint minor = 0;

    checkEgl(
        eglInitialize(display, &major, &minor),
        "eglInitialize"
    );

    std::printf("EGL_VERSION     : %d.%d\n", major, minor);
    std::printf("EGL_VENDOR      : %s\n", safeString(eglQueryString(display, EGL_VENDOR)));
    std::printf("EGL_CLIENT_APIS : %s\n", safeString(eglQueryString(display, EGL_CLIENT_APIS)));

    EGLint count = 0;

    checkEgl(
        eglGetConfigs(display, nullptr, 0, &count),
        "eglGetConfigs count"
    );

    std::vector<EGLConfig> rawConfigs(count);

    checkEgl(
        eglGetConfigs(
            display,
            rawConfigs.data(),
            count,
            &count
        ),
        "eglGetConfigs list"
    );

    std::vector<ConfigInfo> allConfigs;
    allConfigs.reserve(count);

    for (EGLConfig config : rawConfigs) {
        allConfigs.push_back(
            queryConfig(display, config)
        );
    }

    std::sort(
        allConfigs.begin(),
        allConfigs.end(),
        [](const ConfigInfo& a, const ConfigInfo& b) {
            if (a.samples != b.samples) {
                return a.samples > b.samples;
            }

            if (a.depth != b.depth) {
                return a.depth > b.depth;
            }

            if (a.stencil != b.stencil) {
                return a.stencil > b.stencil;
            }

            return a.id < b.id;
        }
    );

    std::vector<ConfigInfo> filtered;

    for (const ConfigInfo& info : allConfigs) {
        if (matchesOptions(info)) {
            filtered.push_back(info);
        }
    }

    printSummary(allConfigs);

    std::printf(
        "\nFiltered configs: %zu\n",
        filtered.size()
    );

    std::printf(
        "Filters: window=%s, pbuffer=%s, es3=%s, depth=%s, stencil=%s, msaa=%s, all=%s\n",
        gOptions.requireWindow ? "yes" : "no",
        gOptions.requirePbuffer ? "yes" : "no",
        gOptions.requireEs3 ? "yes" : "no",
        gOptions.requireDepth ? "yes" : "no",
        gOptions.requireStencil ? "yes" : "no",
        gOptions.requireMsaa ? "yes" : "no",
        gOptions.printAll ? "yes" : "no"
    );

    printConfigTable(filtered);

    runChooseConfigExamples(display);

    checkEgl(
        eglTerminate(display),
        "eglTerminate"
    );

    return EXIT_SUCCESS;
}