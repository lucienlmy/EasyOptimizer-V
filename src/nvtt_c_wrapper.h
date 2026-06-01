#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Texture format values from NVTT 3.2.5's NvttFormat enum.
typedef enum {
    NVTT_FORMAT_BC1 = 1,
    NVTT_FORMAT_BC3 = 4,
    NVTT_FORMAT_BC7 = 15,
} NvttFormat;

// Initialize the NVTT wrapper (loads nvtt30205.dll)
bool nvtt_wrapper_init(void);

// True if nvtt30205.dll is loaded and ready (does not attempt a load on its own
// after a previous failure was cached).
bool nvtt_is_available(void);

// Lightweight startup probe: tries to load nvtt30205.dll and verify the C API is
// present, logging the result, then unloads. Does NOT initialize CUDA.
void nvtt_wrapper_probe(void);

// Shutdown the NVTT wrapper
void nvtt_wrapper_shutdown(void);

// Compress a single image
// Returns true on success, allocates out_data which must be freed by caller
bool nvtt_encode(const uint8_t *bgra_data, int width, int height, NvttFormat format, uint8_t **out_data, size_t *out_size);

#ifdef __cplusplus
}
#endif
