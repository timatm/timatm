#ifndef __NMC_HOST_PLUGIN_IMG_PLACEMENT_CONTIG_H__
#define __NMC_HOST_PLUGIN_IMG_PLACEMENT_CONTIG_H__

#include <stdint.h>
#include <stddef.h>
#include "./common.h"
#include "../flash_config.h"

/* -------------------------------------------------------------------------- */
/*                              image attributes                              */
/* -------------------------------------------------------------------------- */

#define NUM_ROWS_PER_BLK 4
#define SAMPLE_IMG_PATH  "../data/nmc-part-modified.tiff"

#define NUM_CHANNELS_PER_PIXEL 3
#define BYTES_PER_PIXEL        (NUM_CHANNELS_PER_PIXEL)

#define PX_PATCH_WIDTH  512
#define PX_PATCH_HEIGHT (PX_PATCH_WIDTH)
#define PX_PER_PATCH    (PX_PATCH_WIDTH * PX_PATCH_HEIGHT)
#define BYTES_PER_PATCH (PX_PER_PATCH * BYTES_PER_PIXEL)

#define BYTES_PATCH_WIDTH  (PX_PATCH_WIDTH * NUM_CHANNELS_PER_PIXEL)
#define BYTES_PATCH_HEIGHT (PX_PATCH_HEIGHT * NUM_CHANNELS_PER_PIXEL)

#define NUM_STEPS_PER_STEP_GRP (NUM_CHANNELS_PER_PIXEL) // RR policy
#define NUM_STEPS_ON_BLK_X     2
#define NUM_STEPS_ON_BLK_Y     2

#define PX_STEP_WIDTH  2
#define PX_STEP_HEIGHT 1

// static const size_t PX_BLK_WIDTH = (PX_STEP_WIDTH * NUM_STEPS_ON_BLK_X);
#define PX_BLK_WIDTH  (PX_STEP_WIDTH * NUM_STEPS_ON_BLK_X)
#define PX_BLK_HEIGHT (NUM_ROWS_PER_BLK)
#define BYTES_PER_BLK (PX_BLK_WIDTH * PX_BLK_HEIGHT * BYTES_PER_PIXEL)

#define PADDING_DONT_CARE 0

// Image Geometry: (ignore overlapped patches)
//
//                 |---- Image Width ----|
//               - P(0,0) P(0,1) ... P(0,W) ---> Patch Row #0
//               | P(1,0) P(1,1) ... P(1,W)
//  Image Height |  ...
//               - P(H,0) P(H,1) ... P(H,W) ---> Patch Row #H
//                    |                 |
//                    v                 v
//              Patch Col #0      Patch Col #W
//
// Access Patern of Patches => (0,0) (0,1) ... (1,0) (1,1) ... (H,0) ... (H,W)
//

// Patch Geometry:
//
//                 |---- Patch Width ----|
//               - B(0,0) B(0,1) ... B(0,W) ---> Block Row #0
//               | B(1,0) B(1,1) ... B(1,W)
//  Patch Height |  ...
//               - B(H,0) B(H,1) ... B(H,W) ---> Block Row #H
//                    |                 |
//                    v                 v
//              Block Col #0      Block Col #W
//
// Access Patern of Blocks => (0,0) (0,1) ... (1,0) (1,1) ... (H,0) ... (H,W)
//

#define NUM_FULL_BLKS_PER_BLK_ROW (PX_PATCH_WIDTH) // PX_BLK_WIDTH
#define PX_PARTIAL_BLK_WIDTH      (PX_PATCH_WIDTH % PX_BLK_WIDTH)
#define NUM_BLKS_PER_BLK_ROW      (NUM_FULL_BLKS_PER_BLK_ROW + (PX_PARTIAL_BLK_WIDTH > 0))

#define NUM_FULL_BLKS_PER_BLK_COL (PX_PATCH_HEIGHT) // PX_BLK_HEIGHT
#define PX_PARTIAL_BLK_HEIGHT     (PX_PATCH_HEIGHT % PX_BLK_HEIGHT)
#define NUM_BLKS_PER_BLK_COL      (NUM_FULL_BLKS_PER_BLK_COL + (PX_PARTIAL_BLK_HEIGHT > 0))

#define NUM_BLKS_PER_PATCH (NUM_BLKS_PER_BLK_ROW * NUM_BLKS_PER_BLK_COL)

/* -------------------------------------------------------------------------- */
/*                              public interfaces                             */
/* -------------------------------------------------------------------------- */

void dispatch_tiff(const char *path);
void dispatch_image(uint8_t *img, size_t pxWidth, size_t pxHeight);
void dispatch_image_zero_padded(uint8_t *img, size_t pxWidth, size_t pxHeight);

#endif /* __NMC_HOST_PLUGIN_IMG_PLACEMENT_CONTIG_H__ */