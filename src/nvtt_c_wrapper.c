#include "nvtt_c_wrapper.h"
#include "log.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

// Typedefs for the NVTT C API
typedef void* NvttInputOptions;
typedef void* NvttCompressionOptions;
typedef void* NvttOutputOptions;
typedef void* NvttCompressor;

typedef struct {
    void (*beginImage)(int size, int width, int height, int depth, int face, int miplevel);
    bool (*writeData)(const void * data, int size);
    void (*endImage)();
} NvttOutputHandler;

static NvttInputOptions (*p_nvttCreateInputOptions)();
static void (*p_nvttDestroyInputOptions)(NvttInputOptions);
static void (*p_nvttSetInputOptionsFormat)(NvttInputOptions, int);
static void (*p_nvttSetInputOptionsMipmapData)(NvttInputOptions, const void*, int, int, int, int, int);

static NvttCompressionOptions (*p_nvttCreateCompressionOptions)();
static void (*p_nvttDestroyCompressionOptions)(NvttCompressionOptions);
static void (*p_nvttSetCompressionOptionsFormat)(NvttCompressionOptions, int);

static NvttOutputOptions (*p_nvttCreateOutputOptions)();
static void (*p_nvttDestroyOutputOptions)(NvttOutputOptions);
static void (*p_nvttSetOutputOptionsOutputHandler)(NvttOutputOptions, NvttOutputHandler*);
static void (*p_nvttSetOutputOptionsOutputHeader)(NvttOutputOptions, bool);

static NvttCompressor (*p_nvttCreateCompressor)();
static void (*p_nvttDestroyCompressor)(NvttCompressor);
static bool (*p_nvttCompress)(NvttCompressor, const NvttInputOptions, const NvttCompressionOptions, const NvttOutputOptions);
static void (*p_nvttEnableCudaAcceleration)(NvttCompressor, bool);

static HMODULE g_hNvtt = NULL;

bool nvtt_wrapper_init(void) {
    if (g_hNvtt) return true; // Already loaded

    g_hNvtt = LoadLibraryA("nvtt.dll");
    if (!g_hNvtt) {
        LOG_ERR("nvtt_wrapper: Failed to load nvtt.dll");
        return false;
    }

    #define LOAD_FUNC(name) \
        p_##name = (void*)GetProcAddress(g_hNvtt, #name); \
        if (!p_##name) { LOG_ERR("nvtt_wrapper: Missing " #name); FreeLibrary(g_hNvtt); g_hNvtt = NULL; return false; }

    LOAD_FUNC(nvttCreateInputOptions);
    LOAD_FUNC(nvttDestroyInputOptions);
    LOAD_FUNC(nvttSetInputOptionsFormat);
    LOAD_FUNC(nvttSetInputOptionsMipmapData);

    LOAD_FUNC(nvttCreateCompressionOptions);
    LOAD_FUNC(nvttDestroyCompressionOptions);
    LOAD_FUNC(nvttSetCompressionOptionsFormat);

    LOAD_FUNC(nvttCreateOutputOptions);
    LOAD_FUNC(nvttDestroyOutputOptions);
    LOAD_FUNC(nvttSetOutputOptionsOutputHandler);
    LOAD_FUNC(nvttSetOutputOptionsOutputHeader);

    LOAD_FUNC(nvttCreateCompressor);
    LOAD_FUNC(nvttDestroyCompressor);
    LOAD_FUNC(nvttCompress);
    LOAD_FUNC(nvttEnableCudaAcceleration);

    LOG("nvtt_wrapper: nvtt.dll loaded successfully");
    return true;
}

void nvtt_wrapper_shutdown(void) {
    if (g_hNvtt) {
        FreeLibrary(g_hNvtt);
        g_hNvtt = NULL;
    }
}

// Global state for the output handler since we can't pass user_data easily
static uint8_t *g_out_buffer = NULL;
static size_t g_out_size = 0;
static size_t g_out_capacity = 0;

static void out_beginImage(int size, int width, int height, int depth, int face, int miplevel) {
    // We can preallocate
}

static bool out_writeData(const void * data, int size) {
    if (g_out_size + size > g_out_capacity) {
        g_out_capacity = (g_out_capacity == 0) ? size * 2 : (g_out_capacity * 2) + size;
        g_out_buffer = realloc(g_out_buffer, g_out_capacity);
    }
    memcpy(g_out_buffer + g_out_size, data, size);
    g_out_size += size;
    return true;
}

static void out_endImage() {
}

static NvttOutputHandler g_output_handler = {
    out_beginImage,
    out_writeData,
    out_endImage
};

bool nvtt_encode(const uint8_t *bgra_data, int width, int height, NvttFormat format, uint8_t **out_data, size_t *out_size) {
    if (!nvtt_wrapper_init()) return false;

    NvttInputOptions inputOptions = p_nvttCreateInputOptions();
    p_nvttSetInputOptionsFormat(inputOptions, NVTT_INPUT_FORMAT_BGRA_8UB);
    p_nvttSetInputOptionsMipmapData(inputOptions, bgra_data, width, height, 1, 0, 0);

    NvttCompressionOptions compressionOptions = p_nvttCreateCompressionOptions();
    p_nvttSetCompressionOptionsFormat(compressionOptions, format);

    NvttOutputOptions outputOptions = p_nvttCreateOutputOptions();
    p_nvttSetOutputOptionsOutputHeader(outputOptions, false); // No DDS header
    p_nvttSetOutputOptionsOutputHandler(outputOptions, &g_output_handler);

    // Reset buffer
    g_out_size = 0;
    if (g_out_buffer) {
        free(g_out_buffer);
        g_out_buffer = NULL;
        g_out_capacity = 0;
    }

    NvttCompressor compressor = p_nvttCreateCompressor();
    p_nvttEnableCudaAcceleration(compressor, true); // Force CUDA GPU

    bool success = p_nvttCompress(compressor, inputOptions, compressionOptions, outputOptions);

    p_nvttDestroyCompressor(compressor);
    p_nvttDestroyOutputOptions(outputOptions);
    p_nvttDestroyCompressionOptions(compressionOptions);
    p_nvttDestroyInputOptions(inputOptions);

    if (success && g_out_size > 0) {
        *out_data = g_out_buffer;
        *out_size = g_out_size;
        // Detach buffer from globals so caller can free it
        g_out_buffer = NULL;
        g_out_capacity = 0;
        g_out_size = 0;
        return true;
    }

    return false;
}
