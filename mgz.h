#ifndef MGZ_H
#define MGZ_H
#include <stdlib.h>
#include <stdint.h>

/* Compresses INSIZE bytes of data from IN as a whole using compression
   level LEVEL and send the compressed data to a malloc'ed array at *OUT.
   Assumes OUT points to a valid (uint8_t*) pointer.

   Returns the size of the output in bytes and sets *OUT to the
   output array. Returns 0 and sets *OUT to NULL if an error occurs.
   
   Usage:
    uint8_t *out;
    size_t outSize = mgz_deflate(&out, in, inSize, level);
    fwrite(out, 1, outSize, outfile);
*/
size_t mgz_deflate(uint8_t **out, uint8_t *in, size_t inSize, int level);

/* Split INSIZE bytes of data from IN into blocks of size BLOCKSIZE,
   compress each block using compression level LEVEL, and store the
   concatenated result to a malloc'ed array at *OUT. Assumes OUT
   points to a valid (uint8_t*) pointer.

   If BLOCKSIZE is set to 0, a default block size of 1 MiB is used.
   
   Returns the size of the output in bytes and sets *out to the
   output array. Returns 0 and sets *OUT to NULL if an error occurs.
*/
size_t mgz_parallel_deflate(uint8_t **out, uint8_t *in, size_t inSize,
                            int level, size_t blockSize);

#endif // MGZ_H
