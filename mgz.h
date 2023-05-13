#ifndef MGZ_H
#define MGZ_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void *out;
    uint64_t size;
    uint64_t *outBlockSizes;
    uint64_t nOutBlocks;
} mgz_res_t;

/* Compresses INSIZE bytes of data from IN as a whole using compression
   level LEVEL and send the compressed data to a malloc'ed array at *OUT.
   Assumes OUT points to a valid (void*) pointer.

   Returns the size of the output in bytes and sets *OUT to the
   output array. Returns 0 and sets *OUT to NULL if an error occurs.
   
   Usage:
    void *out;
    uint64_t outSize = mgz_deflate(&out, in, inSize, level);
    fwrite(out, 1, outSize, outfile);
    free(out);
*/
uint64_t mgz_deflate(void **out, const void *in, uint64_t inSize, int level);

/* Split INSIZE bytes of data from IN into blocks of size BLOCKSIZE,
   compress each block using compression level LEVEL, and store the
   concatenated result to a malloc'ed array at mgz_res_t.out.

   If BLOCKSIZE is set to 0, a default block size of 1 MiB is used.
   
   Return value contains a pointer to the output stream, the size of
   the output in bytes, a pointer to an array that stores the size of
   each compressed block (or NULL if outBlockSizesNeeded is false),
   and the number of compressed blocks.

   Return value contains all zeros if an error occurs during compression.

   Example usage:
    mgz_res_t res = mgz_parallel_deflate(in, inSize, level, blockSize);
    fwrite(out, 1, outSize, outfile);
    free(out);
*/
mgz_res_t mgz_parallel_deflate(const void *in, uint64_t inSize, int level,
                               uint64_t blockSize, bool outBlockSizesNeeded);

#endif // MGZ_H
