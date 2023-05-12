#include "mgz.h"
#include <assert.h>
#include <malloc.h>
#include <omp.h>
#include <stdbool.h>
#include <string.h>
#include <zlib.h>

#define CHUNK_SIZE 16384
#define ILLEGAL_CHUNK_SIZE (CHUNK_SIZE + 1)
#define DEFAULT_OUT_CAPACITY (CHUNK_SIZE << 1)
#define MIN_BLOCK_SIZE CHUNK_SIZE
#define DEFAULT_BLOCK_SIZE (1ULL << 20) // 1 MiB
#define ILLEGAL_BLOCK_SIZE SIZE_MAX

static uInt load_input(uint8_t *inBuf, const void *in,
                       size_t offset, size_t *remSize) {
    uInt loadSize = 0;

    if (*remSize == 0) return 0;
    if (*remSize < CHUNK_SIZE) loadSize = *remSize;
    else                       loadSize = CHUNK_SIZE;
    *remSize -= loadSize;
    memcpy(inBuf, (void*)((uint8_t*)in + offset), loadSize);

    return loadSize;
}

static uInt copy_output(void **out, size_t outOffset, size_t *outCapacity,
                        uint8_t *outBuf, uInt have) {
    if (outOffset + have > *outCapacity) {
        /* Not enough space, reallocate output array. */
        do (*outCapacity) *= 2; while (outOffset + have > *outCapacity);
        void *newOut = realloc(*out, *outCapacity);
        if (!newOut) return ILLEGAL_CHUNK_SIZE;
        *out = newOut;
    }
    memcpy((void*)((uint8_t*)(*out) + outOffset), outBuf, have);
    return have;
}

static size_t copy_block(void **out, size_t outOffset, size_t *outCapacity,
                         uint8_t *outPart, size_t outPartSize) {
    if (outOffset + outPartSize > *outCapacity) {
        /* Not enough space, reallocate output array. */
        do (*outCapacity) *= 2; while (outOffset + outPartSize > *outCapacity);
        void *newOut = realloc(*out, *outCapacity);
        if (!newOut) return ILLEGAL_BLOCK_SIZE;
        *out = newOut;
    }
    memcpy((void*)((uint8_t*)(*out) + outOffset), outPart, outPartSize);
    return outPartSize;
}

size_t mgz_deflate(void **out, const void *in, size_t inSize, int level) {
    int zRet, flush;
    size_t inOffset = 0, outOffset = 0, outCapacity = DEFAULT_OUT_CAPACITY;
    z_stream strm;
    uint8_t *inBuf = (uint8_t*)malloc(CHUNK_SIZE);
    uint8_t *outBuf = (uint8_t*)malloc(CHUNK_SIZE);
    *out = malloc(outCapacity);
    if (!inBuf || !outBuf || !(*out)) {
        printf("mgz_deflate: malloc failed.\n");
        free(*out); *out = NULL;
        goto _bailout;
    }

    /* Allocate deflate state. */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    zRet = deflateInit2(&strm, level, Z_DEFLATED,
                        15 + 16, 8, Z_DEFAULT_STRATEGY); // +16 for gzip header.
    if (zRet != Z_OK) {
        free(*out); *out = NULL;
        goto _bailout;
    }

    /* Compress until the end of IN. */
    do {
        strm.avail_in = load_input(inBuf, in, inOffset, &inSize);
        inOffset += strm.avail_in;
        flush = (strm.avail_in < CHUNK_SIZE) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = inBuf;

        /* Run deflate() on input until output buffer not full, finish
        compression if all of source has been read in. */
        do {
            strm.avail_out = CHUNK_SIZE;
            strm.next_out = outBuf;
            zRet = deflate(&strm, flush);
            if (zRet == Z_STREAM_ERROR) {
                printf("mgz_deflate: (FATAL) deflate returned Z_STREAM_ERROR.\n");
                exit(1);
            }
            uInt have = CHUNK_SIZE - strm.avail_out;
            uInt copied = copy_output(out, outOffset, &outCapacity, outBuf, have);
            if (copied == ILLEGAL_CHUNK_SIZE) {
                printf("mgz_deflate: output realloc failed.\n");
                (void)deflateEnd(&strm);
                free(*out); *out = NULL;
                outOffset = 0;
                goto _bailout;
            }
            assert(copied == have);
            outOffset += copied;
        } while (strm.avail_out == 0);
    } while (flush != Z_FINISH);

    /* Clean up and return. */
    (void)deflateEnd(&strm);

_bailout:
    free(inBuf);
    free(outBuf);
    return outOffset;
}

size_t mgz_parallel_deflate(void **out, const void *in, size_t inSize,
                            int level, size_t blockSize) {
    if (blockSize == 0) blockSize = DEFAULT_BLOCK_SIZE;
    else if (blockSize < MIN_BLOCK_SIZE) {
        printf("mgz_parallel_deflate: resetting block size %zu to the "
               "minimum required block size %d", blockSize, MIN_BLOCK_SIZE);
        blockSize = MIN_BLOCK_SIZE;
    }
    
    /* Allocate space for the output of each block. */
    size_t nBlocks = (inSize + blockSize - 1) / blockSize; // Round up division.
    size_t outOffset = 0, outCapacity = DEFAULT_OUT_CAPACITY;
    *out = malloc(outCapacity);
    void **outParts = (void**)calloc(nBlocks, sizeof(void*));
    size_t *outPartSizes = (size_t*)malloc(nBlocks * sizeof(size_t));
    if (!(*out) || !outParts || !outPartSizes) {
        printf("mgz_parallel_deflate: malloc failed.\n");
        free(*out); *out = NULL;
        goto _bailout;
    }

    /* Compress each block. */
    bool oom = false;
    #pragma omp parallel for
    for (size_t i = 0; i < nBlocks; ++i) {
        size_t thisBlockSize = (i == nBlocks - 1) ?
                               inSize - i * blockSize : blockSize;
        outPartSizes[i] = mgz_deflate(&outParts[i], (void*)((uint8_t*)in + i * blockSize),
                                      thisBlockSize, level);
        if (outPartSizes[i] == 0) oom = true;
    }
    if (oom) goto _bailout;

    /* Concatenate blocks to form the final output. */
    for (size_t i = 0; i < nBlocks; ++i) {
        size_t copied = copy_block(out, outOffset, &outCapacity,
                                   outParts[i], outPartSizes[i]);
        if (copied == ILLEGAL_BLOCK_SIZE) {
            printf("mgz_parallel_deflate: copy_block realloc failed.\n");
            free(*out); *out = NULL;
            goto _bailout;
        }
        assert(copied == outPartSizes[i]);
        outOffset += copied;
        free(outParts[i]); outParts[i] = NULL;
    }

_bailout:
    if (outParts) for (size_t i = 0; i < nBlocks; ++i) free(outParts[i]);
    free(outParts);
    free(outPartSizes);
    return outOffset;
}
