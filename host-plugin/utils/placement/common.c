#include "./common.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE, for asprintf */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../debug.h"

void _flush_page_to_file(uint8_t iFC, void *data, const char *prefix)
{
    const uint8_t *d = data;

    // snprintf to buffer
    char pageBuffer[BYTES_PER_PAGE * 4] = {0};

    size_t bytesPrinted = 0;
    for (size_t iByte = 0; iByte < BYTES_PER_PAGE - 1; ++iByte)
        bytesPrinted += snprintf(&pageBuffer[bytesPrinted], 5, "%u ", d[iByte]);
    bytesPrinted += snprintf(&pageBuffer[bytesPrinted], 5, "%u\n", d[BYTES_PER_PAGE - 1]);

    // append to file
    char *filename;
    assert_exit(asprintf(&filename, "%s.%u.log", prefix, iFC) != -1, "asprintf failed");

    FILE *f  = fopen(filename, "a");
    size_t n = fwrite(pageBuffer, 1, bytesPrinted, f);
    fclose(f);
    free(filename);

    assert_exit(n == bytesPrinted, "Only %lu bytes are successfully written...", n);
}

void _flush_page_to_file_bin(uint8_t iFC, void *data, const char *prefix)
{
    char *filename;
    assert_exit(asprintf(&filename, "%s.%u.bin", prefix, iFC) != -1, "asprintf failed");

    FILE *f  = fopen(filename, "a");
    size_t n = fwrite(data, 1, BYTES_PER_PAGE, f);
    fclose(f);
    free(filename);

    assert_exit(n == BYTES_PER_PAGE, "Only %lu bytes are successfully written...", n);
}

void _flush_page_to_file_bin_noC(void *data, const char *prefix)
{
    char *filename;
    assert_exit(asprintf(&filename, "%s.bin", prefix) != -1, "asprintf failed");

    FILE *f  = fopen(filename, "a");
    size_t n = fwrite(data, 1, BYTES_PER_PAGE, f);
    fclose(f);
    free(filename);

    assert_exit(n == BYTES_PER_PAGE, "Only %lu bytes are successfully written...", n);
}

void memset_mat(ByteMatrix_t *m, int val, size_t sz)
{
    ARRAY_FROM_BYTE_MATRIX(dst, *m);
    for (size_t iRow = 0; iRow < m->height; ++iRow)
        memset((*dst)[iRow], val, sz);
}

void memcpy_mat(ByteMatrix_t *m, const ByteMatrix_t *s, size_t s_off, size_t sz, size_t rows)
{
    ARRAY_FROM_BYTE_MATRIX(src, *s);
    ARRAY_FROM_BYTE_MATRIX(dst, *m);

    // copy valid region
    for (size_t iRow = 0; iRow < rows; ++iRow)
        memcpy((*dst)[iRow], &(*src)[iRow][s_off], sz);
}
