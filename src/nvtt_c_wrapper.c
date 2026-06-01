#include "nvtt_c_wrapper.h"
#include "log.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

typedef void NvttContext;
typedef void NvttSurface;
typedef void NvttCompressionOptions;
typedef void NvttOutputOptions;
typedef int NvttBoolean;

enum {
    NVTT_FALSE = 0,
    NVTT_TRUE = 1,
    NVTT_INPUT_FORMAT_BGRA_8UB = 0,
};

typedef void (*NvttBeginImageHandler)(int size, int width, int height, int depth, int face, int miplevel);
typedef NvttBoolean (*NvttWriteDataHandler)(const void *data, int size);
typedef void (*NvttEndImageHandler)(void);

static NvttContext *(*p_nvttCreateContext)(void);
static void (*p_nvttDestroyContext)(NvttContext *);
static void (*p_nvttSetContextCudaAcceleration)(NvttContext *, NvttBoolean);
static NvttBoolean (*p_nvttContextCompress)(const NvttContext *, const NvttSurface *, int, int,
                                            const NvttCompressionOptions *, const NvttOutputOptions *);

static NvttSurface *(*p_nvttCreateSurface)(void);
static void (*p_nvttDestroySurface)(NvttSurface *);
static NvttBoolean (*p_nvttSurfaceSetImageData)(NvttSurface *, int, int, int, int, const void *,
                                                NvttBoolean, void *);

static NvttCompressionOptions *(*p_nvttCreateCompressionOptions)(void);
static void (*p_nvttDestroyCompressionOptions)(NvttCompressionOptions *);
static void (*p_nvttSetCompressionOptionsFormat)(NvttCompressionOptions *, int);

static NvttOutputOptions *(*p_nvttCreateOutputOptions)(void);
static void (*p_nvttDestroyOutputOptions)(NvttOutputOptions *);
static void (*p_nvttSetOutputOptionsOutputHandler)(NvttOutputOptions *, NvttBeginImageHandler,
                                                   NvttWriteDataHandler, NvttEndImageHandler);
static void (*p_nvttSetOutputOptionsOutputHeader)(NvttOutputOptions *, NvttBoolean);

static HMODULE g_h_nvtt = NULL;
static NvttContext *g_context = NULL;
static bool g_nvtt_failed = false;
static uint8_t *g_out_buffer = NULL;
static size_t g_out_size = 0;
static size_t g_out_capacity = 0;
static bool g_out_failed = false;

static HMODULE nvtt_load_library(void) {
    wchar_t exe_dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe_dir, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        wchar_t *slash = wcsrchr(exe_dir, L'\\');
        if (slash) {
            *slash = 0;
            wchar_t dll_path[MAX_PATH];
            _snwprintf(dll_path, MAX_PATH, L"%s\\nvtt30205.dll", exe_dir);
            dll_path[MAX_PATH - 1] = 0;
            HMODULE h = LoadLibraryExW(dll_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (h) {
                LOG("nvtt_wrapper: loaded NVTT 3.2.5 from exe dir");
                return h;
            }
            LOG_ERR("nvtt_wrapper: LoadLibraryEx('%ls') failed (GetLastError=%lu)",
                    dll_path, (unsigned long)GetLastError());
        }
    }
    HMODULE h = LoadLibraryA("nvtt30205.dll");
    if (!h)
        LOG_ERR("nvtt_wrapper: LoadLibrary('nvtt30205.dll') failed (GetLastError=%lu)",
                (unsigned long)GetLastError());
    return h;
}

static bool nvtt_load_exports(HMODULE h) {
    #define LOAD_FUNC(name) do { \
        p_##name = (void *)GetProcAddress(h, #name); \
        if (!p_##name) { LOG_ERR("nvtt_wrapper: missing NVTT 3 export " #name); return false; } \
    } while (0)

    LOAD_FUNC(nvttCreateContext);
    LOAD_FUNC(nvttDestroyContext);
    LOAD_FUNC(nvttSetContextCudaAcceleration);
    LOAD_FUNC(nvttContextCompress);
    LOAD_FUNC(nvttCreateSurface);
    LOAD_FUNC(nvttDestroySurface);
    LOAD_FUNC(nvttSurfaceSetImageData);
    LOAD_FUNC(nvttCreateCompressionOptions);
    LOAD_FUNC(nvttDestroyCompressionOptions);
    LOAD_FUNC(nvttSetCompressionOptionsFormat);
    LOAD_FUNC(nvttCreateOutputOptions);
    LOAD_FUNC(nvttDestroyOutputOptions);
    LOAD_FUNC(nvttSetOutputOptionsOutputHandler);
    LOAD_FUNC(nvttSetOutputOptionsOutputHeader);

    #undef LOAD_FUNC
    return true;
}

bool nvtt_is_available(void) {
    return g_h_nvtt != NULL && g_context != NULL;
}

void nvtt_wrapper_probe(void) {
    HMODULE h = nvtt_load_library();
    if (!h) {
        LOG("nvtt probe: NVTT 3.2.5 unavailable -> GPU encoding unavailable (CPU only)");
        return;
    }
    if (GetProcAddress(h, "nvttCreateContext") && GetProcAddress(h, "nvttContextCompress"))
        LOG("nvtt probe: NVTT 3 C API present -> BC1/BC3/BC7 GPU encoding available when toggled");
    else
        LOG("nvtt probe: nvtt30205.dll loaded but NVTT 3 C API missing -> incompatible build");
    FreeLibrary(h);
}

bool nvtt_wrapper_init(void) {
    if (nvtt_is_available()) return true;
    if (g_nvtt_failed) return false;

    g_h_nvtt = nvtt_load_library();
    if (!g_h_nvtt || !nvtt_load_exports(g_h_nvtt)) {
        if (g_h_nvtt) FreeLibrary(g_h_nvtt);
        g_h_nvtt = NULL;
        g_nvtt_failed = true;
        return false;
    }

    g_context = p_nvttCreateContext();
    if (!g_context) {
        LOG_ERR("nvtt_wrapper: nvttCreateContext failed");
        FreeLibrary(g_h_nvtt);
        g_h_nvtt = NULL;
        g_nvtt_failed = true;
        return false;
    }
    p_nvttSetContextCudaAcceleration(g_context, NVTT_TRUE);
    LOG("nvtt_wrapper: NVTT 3.2.5 context ready (BC1/BC3/BC7 GPU/CUDA encoder)");
    return true;
}

void nvtt_wrapper_shutdown(void) {
    free(g_out_buffer);
    g_out_buffer = NULL;
    g_out_size = 0;
    g_out_capacity = 0;
    if (g_context) {
        p_nvttDestroyContext(g_context);
        g_context = NULL;
    }
    if (g_h_nvtt) {
        FreeLibrary(g_h_nvtt);
        g_h_nvtt = NULL;
    }
}

static void out_begin_image(int size, int width, int height, int depth, int face, int miplevel) {
    (void)size; (void)width; (void)height; (void)depth; (void)face; (void)miplevel;
}

static NvttBoolean out_write_data(const void *data, int size) {
    if (!data || size < 0 || g_out_failed) return NVTT_FALSE;
    if (g_out_size + (size_t)size > g_out_capacity) {
        size_t new_capacity = g_out_capacity ? (g_out_capacity * 2) + (size_t)size : (size_t)size * 2;
        uint8_t *grown = (uint8_t *)realloc(g_out_buffer, new_capacity);
        if (!grown) {
            g_out_failed = true;
            return NVTT_FALSE;
        }
        g_out_buffer = grown;
        g_out_capacity = new_capacity;
    }
    memcpy(g_out_buffer + g_out_size, data, (size_t)size);
    g_out_size += (size_t)size;
    return NVTT_TRUE;
}

static void out_end_image(void) {
}

bool nvtt_encode(const uint8_t *bgra_data, int width, int height, NvttFormat format,
                 uint8_t **out_data, size_t *out_size) {
    if (!bgra_data || width <= 0 || height <= 0 || !out_data || !out_size) return false;
    *out_data = NULL;
    *out_size = 0;
    if (!nvtt_wrapper_init()) return false;

    NvttSurface *surface = p_nvttCreateSurface();
    NvttCompressionOptions *compression = p_nvttCreateCompressionOptions();
    NvttOutputOptions *output = p_nvttCreateOutputOptions();
    if (!surface || !compression || !output) goto cleanup;

    if (!p_nvttSurfaceSetImageData(surface, NVTT_INPUT_FORMAT_BGRA_8UB, width, height, 1,
                                   bgra_data, NVTT_FALSE, NULL))
        goto cleanup;

    p_nvttSetCompressionOptionsFormat(compression, format);
    p_nvttSetOutputOptionsOutputHeader(output, NVTT_FALSE);
    p_nvttSetOutputOptionsOutputHandler(output, out_begin_image, out_write_data, out_end_image);

    free(g_out_buffer);
    g_out_buffer = NULL;
    g_out_size = 0;
    g_out_capacity = 0;
    g_out_failed = false;

    if (p_nvttContextCompress(g_context, surface, 0, 0, compression, output) &&
        !g_out_failed && g_out_size > 0) {
        *out_data = g_out_buffer;
        *out_size = g_out_size;
        g_out_buffer = NULL;
        g_out_size = 0;
        g_out_capacity = 0;
    }

cleanup:
    if (output) p_nvttDestroyOutputOptions(output);
    if (compression) p_nvttDestroyCompressionOptions(compression);
    if (surface) p_nvttDestroySurface(surface);
    if (*out_data) return true;

    free(g_out_buffer);
    g_out_buffer = NULL;
    g_out_size = 0;
    g_out_capacity = 0;
    return false;
}
