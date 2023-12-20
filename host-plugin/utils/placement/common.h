#ifndef __NMC_HOST_PLUGIN_PLACEMENT_COMMON_H__
#define __NMC_HOST_PLUGIN_PLACEMENT_COMMON_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../flash_config.h"

#define ALIGN_UP(x, align_to) (((x) + ((align_to)-1)) & ~((align_to)-1))

typedef struct
{
    uint8_t *base;
    size_t width;
    size_t height;
} ByteMatrix_t;

#define INIT_BYTE_MATRIX(h, w)                                                                     \
    {                                                                                              \
        .height = (h), .width = (w), .base = calloc(((h) * (w)), 1)                                \
    }

#define MAT_2D_ARRAY(type, mat)          (type(*)[(mat).height][(mat).width])(mat).base
#define ARRAY_FROM_BYTE_MATRIX(var, mat) uint8_t(*var)[][(mat).width] = MAT_2D_ARRAY(uint8_t, mat)

/* -------------------------------------------------------------------------- */
/*                                  functions                                 */
/* -------------------------------------------------------------------------- */

void memset_mat(ByteMatrix_t *m, int val, size_t sz);
void memcpy_mat(ByteMatrix_t *m, const ByteMatrix_t *s, size_t s_off, size_t sz, size_t rows);

void _flush_page_to_file(uint8_t iFC, void *data, const char *prefix);
void _flush_page_to_file_bin(uint8_t iFC, void *data, const char *prefix);
void _flush_page_to_file_bin_noC(void *data, const char *prefix);
#endif /* __NMC_HOST_PLUGIN_PLACEMENT_COMMON_H__ */