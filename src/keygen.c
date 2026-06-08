/*
 * keygen.c — Derive ng.dat / lut.dat from the user's GTA5.exe using magic.dat.
 *
 * Modern versions of GTA5.exe no longer contain the NG keys and decryption tables
 * in plain text. Instead, we bundle a pre-encrypted payload (`magic.dat`) which
 * contains these keys. We scan the user's GTA5.exe purely for the AES key, which
 * is then used to decrypt `magic.dat`.
 *
 * The `magic.dat` decryption process:
 * 1. Scan GTA5.exe for the AES key (using its SHA-1 hash).
 * 2. Read `magic.dat`.
 * 3. De-obfuscate `magic.dat` using a subtractive PRNG seeded with the JenkHash of the AES key.
 * 4. Decrypt the result using AES-256-ECB (via Windows BCrypt).
 * 5. Decompress the result using DEFLATE (miniz).
 * 6. The decompressed buffer contains `ng.dat` (306000 bytes) + `lut.dat` (256 bytes) + awc_key (16 bytes).
 */

#include "keygen.h"
#include "log.h"
#include "hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <bcrypt.h>

#define MINIZ_NO_STDIO
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_HEADER_FILE_ONLY
#include "miniz.h"

#pragma comment(lib, "bcrypt.lib")

#define SHA1_LEN          20
#define AES_WINDOW        0x20    /* 32 bytes */
#define SCAN_ALIGN        8

#define NG_KEYS_SIZE      27472
#define NG_TABLES_SIZE    278528
#define NG_DAT_SIZE       (NG_KEYS_SIZE + NG_TABLES_SIZE)  /* 306000 */
#define LUT_DAT_SIZE      256

/* Hardcoded SHA-1 hash of the PC_AES_KEY */
static const uint8_t g_aes_key_hash[20] = {
    0xA0, 0x79, 0x61, 0x28, 0xA7, 0x75, 0x72, 0x0A, 
    0xC2, 0x04, 0xD9, 0x81, 0x9F, 0x68, 0xC1, 0x72, 
    0xE3, 0x95, 0x2C, 0x6D
};

/* ── C# System.Random implementation (Knuth subtractive generator) ─────── */
typedef struct {
    int inext;
    int inextp;
    int SeedArray[56];
} CSRandom;

static void csrandom_init(CSRandom *r, int seed) {
    int ii, mj, mk;
    int subtraction = (seed == -2147483648) ? 2147483647 : (seed < 0 ? -seed : seed);
    mj = 161803398 - subtraction;
    r->SeedArray[55] = mj;
    mk = 1;
    for (int i = 1; i < 55; i++) {
        ii = (21 * i) % 55;
        r->SeedArray[ii] = mk;
        mk = mj - mk;
        if (mk < 0) mk += 2147483647;
        mj = r->SeedArray[ii];
    }
    for (int k = 1; k < 5; k++) {
        for (int i = 1; i < 56; i++) {
            r->SeedArray[i] -= r->SeedArray[1 + (i + 30) % 55];
            if (r->SeedArray[i] < 0) r->SeedArray[i] += 2147483647;
        }
    }
    r->inext = 0;
    r->inextp = 21;
}

static int csrandom_next(CSRandom *r) {
    int retVal;
    int locINext = r->inext;
    int locINextp = r->inextp;
    if (++locINext >= 56) locINext = 1;
    if (++locINextp >= 56) locINextp = 1;
    retVal = r->SeedArray[locINext] - r->SeedArray[locINextp];
    if (retVal == 2147483647) retVal--;
    if (retVal < 0) retVal += 2147483647;
    r->SeedArray[locINext] = retVal;
    r->inext = locINext;
    r->inextp = locINextp;
    return retVal;
}

static void csrandom_next_bytes(CSRandom *r, uint8_t *buffer, size_t length) {
    for (size_t i = 0; i < length; i++) {
        buffer[i] = (uint8_t)(csrandom_next(r) % 256);
    }
}

/* ── helpers ───────────────────────────────────────────────────────────── */

static uint8_t *read_whole_file(const wchar_t *path, size_t *out_size) {
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    _fseeki64(f, 0, SEEK_END);
    long long sz = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

static bool exe_dir_path(const wchar_t *leaf, wchar_t *out, size_t count) {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, (DWORD)MAX_PATH)) return false;
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) *slash = 0;
    _snwprintf(out, count, L"%s\\%s", exe, leaf);
    out[count - 1] = 0;
    return true;
}

typedef struct {
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE hash;
    DWORD obj_len;
    uint8_t *obj;
    bool reusable;
} Sha1Ctx;

static bool sha1_init(Sha1Ctx *c) {
    memset(c, 0, sizeof(*c));
    c->reusable = true;
    if (BCryptOpenAlgorithmProvider(&c->alg, BCRYPT_SHA1_ALGORITHM, NULL,
                                    BCRYPT_HASH_REUSABLE_FLAG) < 0) {
        c->reusable = false;
        if (BCryptOpenAlgorithmProvider(&c->alg, BCRYPT_SHA1_ALGORITHM, NULL, 0) < 0)
            return false;
    }
    DWORD cb = 0;
    if (BCryptGetProperty(c->alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&c->obj_len,
                          sizeof(c->obj_len), &cb, 0) < 0) return false;
    c->obj = (uint8_t *)malloc(c->obj_len);
    if (!c->obj) return false;
    if (c->reusable) {
        if (BCryptCreateHash(c->alg, &c->hash, c->obj, c->obj_len, NULL, 0, 0) < 0)
            return false;
    }
    return true;
}

static void sha1_free(Sha1Ctx *c) {
    if (c->hash) BCryptDestroyHash(c->hash);
    if (c->alg) BCryptCloseAlgorithmProvider(c->alg, 0);
    free(c->obj);
    memset(c, 0, sizeof(*c));
}

static bool sha1_compute(Sha1Ctx *c, const uint8_t *data, size_t len, uint8_t out[20]) {
    if (c->reusable) {
        return BCryptHashData(c->hash, (PUCHAR)data, (ULONG)len, 0) >= 0 &&
               BCryptFinishHash(c->hash, out, 20, 0) >= 0;
    }
    BCRYPT_HASH_HANDLE h = NULL;
    if (BCryptCreateHash(c->alg, &h, c->obj, c->obj_len, NULL, 0, 0) < 0) return false;
    bool ok = BCryptHashData(h, (PUCHAR)data, (ULONG)len, 0) >= 0 &&
              BCryptFinishHash(h, out, 20, 0) >= 0;
    BCryptDestroyHash(h);
    return ok;
}

static bool scan_for_aes_key(const uint8_t *exe, size_t exe_size, uint8_t out_key[32],
                             KeygenProgress progress, void *ctx) {
    Sha1Ctx sc;
    if (!sha1_init(&sc)) return false;

    if (exe_size < AES_WINDOW) { sha1_free(&sc); return false; }
    size_t last_pos = exe_size - AES_WINDOW;
    size_t step_total = last_pos / SCAN_ALIGN + 1;
    size_t step = 0;
    int last_pct = -1;

    for (size_t pos = 0; pos <= last_pos; pos += SCAN_ALIGN, step++) {
        uint8_t digest[20];
        if (!sha1_compute(&sc, exe + pos, AES_WINDOW, digest)) continue;

        if (memcmp(digest, g_aes_key_hash, SHA1_LEN) == 0) {
            memcpy(out_key, exe + pos, AES_WINDOW);
            sha1_free(&sc);
            return true;
        }

        if (progress && (step & 0x3FFF) == 0) {
            int pct = 2 + (int)(38LL * step / (step_total ? step_total : 1));
            if (pct != last_pct) {
                progress(L"Scanning for AES Key...", pct, ctx);
                last_pct = pct;
            }
        }
    }

    sha1_free(&sc);
    return false;
}

static bool decrypt_aes_ecb(uint8_t *data, size_t data_len, const uint8_t *key) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    bool ok = false;
    
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0) < 0) goto cleanup;
    if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB, sizeof(BCRYPT_CHAIN_MODE_ECB), 0) < 0) goto cleanup;

    DWORD keyObjSize = 0;
    DWORD cbData = 0;
    if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&keyObjSize, sizeof(keyObjSize), &cbData, 0) < 0) goto cleanup;
    
    uint8_t *keyObj = (uint8_t *)malloc(keyObjSize);
    if (!keyObj) goto cleanup;

    if (BCryptGenerateSymmetricKey(hAlg, &hKey, keyObj, keyObjSize, (PUCHAR)key, 32, 0) < 0) {
        free(keyObj);
        goto cleanup;
    }

    /* In-place decryption */
    ULONG cbResult = 0;
    if (BCryptDecrypt(hKey, data, (ULONG)data_len, NULL, NULL, 0, data, (ULONG)data_len, &cbResult, 0) >= 0) {
        ok = true;
    }

    BCryptDestroyKey(hKey);
    free(keyObj);
cleanup:
    if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    return ok;
}

/* ── public ────────────────────────────────────────────────────────────── */

bool keygen_from_exe(const wchar_t *exe_path, const wchar_t *out_dir,
                     KeygenProgress progress, void *ctx,
                     char *error, size_t error_size) {
    #define FAIL(msg) do { if (error && error_size) { strncpy(error, msg, error_size - 1); error[error_size-1]=0; } goto cleanup; } while (0)

    bool result = false;
    uint8_t *exe = NULL, *magic = NULL, *decrypted = NULL, *inflated = NULL;
    size_t exe_size = 0, magic_size = 0;
    uint8_t aes_key[32];

    /* 1. Load magic.dat */
    wchar_t magic_path[MAX_PATH];
    if (!exe_dir_path(L"magic.dat", magic_path, MAX_PATH))
        FAIL("cannot locate application directory");
    magic = read_whole_file(magic_path, &magic_size);
    if (!magic || magic_size == 0)
        FAIL("magic.dat missing (must sit next to the .exe)");

    /* 2. Load the GTA5 executable. */
    if (progress) progress(L"Reading GTA5.exe...", 2, ctx);
    exe = read_whole_file(exe_path, &exe_size);
    if (!exe) FAIL("could not read the selected GTA5.exe");
    if (exe_size < 16 * 1024 * 1024)
        FAIL("selected file is too small to be GTA5.exe");

    /* 3. Scan for the AES key in the executable */
    if (!scan_for_aes_key(exe, exe_size, aes_key, progress, ctx))
        FAIL("AES key not found — wrong/updated GTA5.exe");

    if (progress) progress(L"AES Key found. Decrypting keys...", 50, ctx);

    /* 4. De-obfuscate magic.dat using JenkHash(AES_KEY) and PRNG */
    uint32_t seed = jenk_hash_data(aes_key, sizeof(aes_key));
    CSRandom rnd;
    csrandom_init(&rnd, (int)seed);
    
    uint8_t *rb1 = (uint8_t *)malloc(magic_size);
    uint8_t *rb2 = (uint8_t *)malloc(magic_size);
    uint8_t *rb3 = (uint8_t *)malloc(magic_size);
    uint8_t *rb4 = (uint8_t *)malloc(magic_size);
    if (!rb1 || !rb2 || !rb3 || !rb4) {
        free(rb1); free(rb2); free(rb3); free(rb4);
        FAIL("out of memory allocating de-obfuscation buffers");
    }

    csrandom_next_bytes(&rnd, rb1, magic_size);
    csrandom_next_bytes(&rnd, rb2, magic_size);
    csrandom_next_bytes(&rnd, rb3, magic_size);
    csrandom_next_bytes(&rnd, rb4, magic_size);

    decrypted = (uint8_t *)malloc(magic_size);
    if (!decrypted) {
        free(rb1); free(rb2); free(rb3); free(rb4);
        FAIL("out of memory allocating decryption buffer");
    }

    for (size_t i = 0; i < magic_size; i++) {
        decrypted[i] = (uint8_t)(magic[i] - rb1[i] - rb2[i] - rb3[i] - rb4[i]);
    }

    free(rb1); free(rb2); free(rb3); free(rb4);

    if (progress) progress(L"Applying AES layer...", 60, ctx);

    /* 5. Decrypt using AES-256-ECB */
    /* CodeWalker rounds length down to a multiple of 16 */
    size_t aes_len = magic_size - (magic_size % 16);
    if (aes_len > 0) {
        if (!decrypt_aes_ecb(decrypted, aes_len, aes_key))
            FAIL("AES decryption of magic.dat failed");
    }

    if (progress) progress(L"Decompressing keys...", 70, ctx);

    /* 6. Inflate using DEFLATE (raw deflate stream, no zlib header) */
    /* CodeWalker bundles AWC_KEY at the end, so total size is: 27472 + 278528 + 256 + 16 = 306272 bytes */
    size_t expected_inflated_size = NG_KEYS_SIZE + NG_TABLES_SIZE + LUT_DAT_SIZE + 16;
    inflated = (uint8_t *)malloc(expected_inflated_size);
    if (!inflated) FAIL("out of memory allocating inflate buffer");

    mz_stream stream;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = decrypted;
    stream.avail_in = (mz_uint32)magic_size;
    stream.next_out = inflated;
    stream.avail_out = (mz_uint32)expected_inflated_size;

    if (mz_inflateInit2(&stream, -15) != MZ_OK) {
        FAIL("failed to initialize inflate");
    }

    int status = mz_inflate(&stream, MZ_FINISH);
    mz_inflateEnd(&stream);

    if (status != MZ_STREAM_END && status != MZ_OK) {
        FAIL("failed to decompress magic.dat payload (raw deflate)");
    }

    if (progress) progress(L"Writing ng.dat / lut.dat...", 90, ctx);

    /* 7. Write out ng.dat and lut.dat */
    wchar_t ng_path[MAX_PATH], lut_path[MAX_PATH];
    _snwprintf(ng_path, MAX_PATH, L"%s\\ng.dat", out_dir);   ng_path[MAX_PATH-1]=0;
    _snwprintf(lut_path, MAX_PATH, L"%s\\lut.dat", out_dir); lut_path[MAX_PATH-1]=0;

    FILE *f = _wfopen(ng_path, L"wb");
    if (!f) FAIL("could not write ng.dat (folder not writable?)");
    bool wok = fwrite(inflated, 1, NG_DAT_SIZE, f) == NG_DAT_SIZE; /* 306000 bytes */
    fclose(f);
    if (!wok) FAIL("failed writing ng.dat");

    f = _wfopen(lut_path, L"wb");
    if (!f) FAIL("could not write lut.dat");
    wok = fwrite(inflated + NG_DAT_SIZE, 1, LUT_DAT_SIZE, f) == LUT_DAT_SIZE; /* 256 bytes */
    fclose(f);
    if (!wok) FAIL("failed writing lut.dat");

    if (progress) progress(L"Keys generated successfully.", 100, ctx);
    LOG("keygen: ng.dat + lut.dat extracted from magic.dat using AES key");
    result = true;

cleanup:
    free(exe); free(magic); free(decrypted); free(inflated);
    return result;
    #undef FAIL
}
