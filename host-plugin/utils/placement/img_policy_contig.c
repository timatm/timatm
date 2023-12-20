#include "img_policy_contig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "tiffio.h" // apt install libtiff5-dev, gcc -ltiff

#include "../debug.h"

void dispatch_patch(size_t iPatch, const ByteMatrix_t *patch, size_t pxHeight, size_t pxWidth);
void dispatch_blk_row(const ByteMatrix_t *blkRow, size_t pxWidth);
void flush_img_fc_buffer(bool force);

/* -------------------------------------------------------------------------- */
/*                allow users define their flush handling logic               */
/* -------------------------------------------------------------------------- */

#if (USER_FLUSH_IMAGE == true)
#pragma message "Use user defined logic for IMAGE flush"
extern void flush_page_image(uint8_t iFC, uint8_t *data);
#else
#pragma message "Use default logic for IMAGE flush (flush as text file)"
void flush_page_image(uint8_t iFC, uint8_t *data)
{
    _flush_page_to_file(iFC, data, "logs/image-buffer");
}
#endif /* USER_FLUSH_IMAGE */

/* -------------------------------------------------------------------------- */
/*                   internal members and utility functions                   */
/* -------------------------------------------------------------------------- */

/*
 * FC_BUFFERS will be flushed after dispatching a blockRow, but the data will not
 * be flushed if the buffered data size < BYTES_PER_PAGE, therefore the max width
 * of the FC_BUFFERS should be `BYTES_BLK_ROW_WIDTH + BYTES_PER_PAGE`, where the
 * `BYTES_BLK_ROW_WIDTH` should be less than, or equal to, `BYTES_PATCH_WIDTH`.
 */

static size_t FC_BUFFER_SZ;
static uint8_t FC_BUFFERS[NUM_FLASH_CHANNELS][BYTES_PATCH_WIDTH + BYTES_PER_PAGE];

static size_t get_fc_buffer_sz() { return FC_BUFFER_SZ; }

/* -------------------------------------------------------------------------- */
/*                     implementation of public functions                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Split the image into patches and dispatch with a row major policy
 *
 * @param imgFlatten The flatten RGB image
 * @param pxWidth The width of the given image in pixels
 * @param pxHeight The height of the given image in pixels
 */
void dispatch_image(uint8_t *imgFlatten, size_t pxWidth, size_t pxHeight)
{
    const size_t pxPatchRowWidth      = pxWidth;
    const size_t bytesImgWidth        = pxWidth * BYTES_PER_PIXEL;
    const size_t bytesPatchRowWidth   = pxPatchRowWidth * BYTES_PER_PIXEL;
    const size_t numFullWidPatches    = pxPatchRowWidth / PX_PATCH_WIDTH;
    const size_t pxPartWidPatchWid    = pxPatchRowWidth % PX_PATCH_WIDTH;
    const size_t bytesPartWidPatchWid = pxPartWidPatchWid * BYTES_PER_PIXEL;

    // allocate buffers for storing the whole patch row and single patch
    size_t idxPatch        = 0;
    size_t bytesImgHandled = 0;

    // allocate for patch row buffer and patch buffer
    ByteMatrix_t matPatch = INIT_BYTE_MATRIX(PX_PATCH_HEIGHT, BYTES_PATCH_WIDTH);
    assert_exit(matPatch.base, "Failed to allocate memory for ByteMatrix");

    ByteMatrix_t matPatchRow = INIT_BYTE_MATRIX(PX_PATCH_HEIGHT, bytesPatchRowWidth);
    assert_exit(matPatchRow.base, "Failed to allocate memory for ByteMatrix");

    ARRAY_FROM_BYTE_MATRIX(patchRow, matPatchRow);

    // parse the image patch row by patch row
    size_t idxTargetBufRow = 0;
    for (size_t iImgRow = 0; iImgRow < pxHeight; ++iImgRow)
    {
        // copy image row data to the buffer
        memcpy((*patchRow)[idxTargetBufRow], &imgFlatten[bytesImgHandled], bytesImgWidth);

        // update current buffer info
        idxTargetBufRow += 1;
        bytesImgHandled += bytesImgWidth;

        // if buffer full, flush this patch row
        if (idxTargetBufRow == PX_PATCH_HEIGHT)
        {
            idxTargetBufRow = 0;

            // handle full width patches
            size_t bytesHandledWid = 0;
            for (size_t iFullWidPatch = 0; iFullWidPatch < numFullWidPatches; ++iFullWidPatch)
            {
                // copy to patch buffer
                memcpy_mat(&matPatch, &matPatchRow, bytesHandledWid, BYTES_PATCH_WIDTH,
                           PX_PATCH_HEIGHT);
                dispatch_patch(idxPatch, &matPatch, PX_PATCH_HEIGHT, PX_PATCH_WIDTH);

                idxPatch += 1;
                bytesHandledWid += BYTES_PATCH_WIDTH;
            }

            // handle partial width patch (if exists)
            if (pxPartWidPatchWid > 0)
            {
                // copy to patch buffer
                memcpy_mat(&matPatch, &matPatchRow, bytesHandledWid, bytesPartWidPatchWid,
                           PX_PATCH_HEIGHT);
                dispatch_patch(idxPatch, &matPatch, PX_PATCH_HEIGHT, pxPartWidPatchWid);

                idxPatch += 1;
            }
        }
    }

    // all image rows have been handled, but some may still in buffer
    assert_exit((idxTargetBufRow < PX_PATCH_HEIGHT), "Unexpected idxTargetBufRow: %lu",
                idxTargetBufRow);

    if (idxTargetBufRow > 0)
    {
        // the buffer may contain garbages, do memset 0 for padding zeros
        // memset_mat(patch, PX_PATCH_HEIGHT, BYTES_PATCH_WIDTH, 0);

        // dispatch partial height full width patches
        size_t bytesHandledWid = 0;
        for (size_t iFullWidPatch = 0; iFullWidPatch < numFullWidPatches; ++iFullWidPatch)
        {
            memcpy_mat(&matPatch, &matPatchRow, bytesHandledWid, BYTES_PATCH_WIDTH,
                       idxTargetBufRow);
            dispatch_patch(idxPatch, &matPatch, idxTargetBufRow, PX_PATCH_WIDTH);

            idxPatch += 1;
            bytesHandledWid += BYTES_PATCH_WIDTH;
        }

        // dispatch partial height partial width patch if exists
        if (pxPartWidPatchWid > 0)
        {
            memcpy_mat(&matPatch, &matPatchRow, bytesHandledWid, bytesPartWidPatchWid,
                       idxTargetBufRow);
            dispatch_patch(idxPatch, &matPatch, idxTargetBufRow, pxPartWidPatchWid);

            idxPatch += 1;
        }
    }

    // if some data (< page size) still in buffers, force flush
    if (get_fc_buffer_sz() > 0)
    {
        pr_info("FC_BUFFERS still not empty but the image_dispatch is ended, "
                "force flush");
        flush_img_fc_buffer(true);
    }

    free(matPatch.base);
    free(matPatchRow.base);
}

void dispatch_image_zero_padded(uint8_t *img, size_t pxWidth, size_t pxHeight) {}

void dispatch_tiff(const char *path)
{

    assert_exit(path != NULL, "Path not given!");

    TIFF *tif = TIFFOpen(path, "r");
    assert_exit(tif != NULL, "Failed to open TIFF file '%s'", path);

    // get image attr: http://www.simplesystems.org/libtiff/functions/TIFFGetField.html
    uint16_t cfgPlanar;
    uint32_t pxHeight, pxWidth;

    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &pxWidth);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &pxHeight);
    TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &cfgPlanar);

    pr_info("%s (%u, %u)", path, pxHeight, pxWidth);

    // prepare buffer for tiff image
    tsize_t sz = TIFFScanlineSize(tif);
    assert_exit(sz == (pxWidth * 3), "Line size should be 3 x pxWidth, but got %lu", sz);

    tdata_t buf = _TIFFmalloc(sz);
    pr_debug("buffer addr: %p", buf);

    if (cfgPlanar == PLANARCONFIG_CONTIG)
    {
        const size_t pxPatchRowWidth      = pxWidth;
        const size_t bytesImgWidth        = pxWidth * BYTES_PER_PIXEL;
        const size_t bytesPatchRowWidth   = pxPatchRowWidth * BYTES_PER_PIXEL;
        const size_t numFullWidPatches    = pxPatchRowWidth / PX_PATCH_WIDTH;
        const size_t pxPartWidPatchWid    = pxPatchRowWidth % PX_PATCH_WIDTH;
        const size_t bytesPartWidPatchWid = pxPartWidPatchWid * BYTES_PER_PIXEL;

        // allocate buffers for storing the whole patch row and single patch
        size_t idxPatch        = 0;
        size_t bytesImgHandled = 0;

        // allocate for patch row buffer and patch buffer
        ByteMatrix_t matPatch = INIT_BYTE_MATRIX(PX_PATCH_HEIGHT, BYTES_PATCH_WIDTH);
        assert_exit(matPatch.base, "Failed to allocate memory for ByteMatrix");

        ByteMatrix_t matPatchRow = INIT_BYTE_MATRIX(PX_PATCH_HEIGHT, bytesPatchRowWidth);
        assert_exit(matPatchRow.base, "Failed to allocate memory for ByteMatrix");

        ARRAY_FROM_BYTE_MATRIX(patchRow, matPatchRow);

        size_t idxTargetBufRow = 0;
        for (size_t iImgRow = 0; iImgRow < pxHeight; ++iImgRow)
        {
            // read line from tiff (the sample param is used in PlanarConfiguration == 2)
            TIFFReadScanline(tif, buf, iImgRow, 0);

            // copy image row data to the buffer
            memcpy((*patchRow)[idxTargetBufRow], buf, bytesImgWidth);

            // update current buffer info
            idxTargetBufRow += 1;
            bytesImgHandled += bytesImgWidth;

            // if buffer full, flush this patch row
            if (idxTargetBufRow == PX_PATCH_HEIGHT)
            {
                idxTargetBufRow = 0;

                // handle full width patches
                size_t bytesHandledWid = 0;
                for (size_t iFullWidPatch = 0; iFullWidPatch < numFullWidPatches; ++iFullWidPatch)
                {
                    // copy to patch buffer
                    memcpy_mat(&matPatch, &matPatchRow, bytesHandledWid, BYTES_PATCH_WIDTH,
                               PX_PATCH_HEIGHT);
                    dispatch_patch(idxPatch, &matPatch, PX_PATCH_HEIGHT, PX_PATCH_WIDTH);

                    idxPatch += 1;
                    bytesHandledWid += BYTES_PATCH_WIDTH;
                }

                // handle partial width patch (if exists)
                if (pxPartWidPatchWid > 0)
                {
                    // copy to patch buffer
                    memcpy_mat(&matPatch, &matPatchRow, bytesHandledWid, bytesPartWidPatchWid,
                               PX_PATCH_HEIGHT);
                    dispatch_patch(idxPatch, &matPatch, PX_PATCH_HEIGHT, pxPartWidPatchWid);

                    idxPatch += 1;
                }
            }
        }

        // all image rows have been handled, but some may still in buffer
        assert_exit((idxTargetBufRow < PX_PATCH_HEIGHT), "Unexpected idxTargetBufRow: %lu",
                    idxTargetBufRow);

        if (idxTargetBufRow > 0)
        {
            // the buffer may contain garbages, do memset 0 for padding zeros
            // memset_mat(patch, PX_PATCH_HEIGHT, BYTES_PATCH_WIDTH, 0);

            // dispatch partial height full width patches
            size_t bytesHandledWid = 0;
            for (size_t iFullWidPatch = 0; iFullWidPatch < numFullWidPatches; ++iFullWidPatch)
            {
                memcpy_mat(&matPatch, &matPatchRow, bytesHandledWid, BYTES_PATCH_WIDTH,
                           idxTargetBufRow);
                dispatch_patch(idxPatch, &matPatch, idxTargetBufRow, PX_PATCH_WIDTH);

                idxPatch += 1;
                bytesHandledWid += BYTES_PATCH_WIDTH;
            }

            // dispatch partial height partial width patch if exists
            if (pxPartWidPatchWid > 0)
            {
                memcpy_mat(&matPatch, &matPatchRow, bytesHandledWid, bytesPartWidPatchWid,
                           idxTargetBufRow);
                dispatch_patch(idxPatch, &matPatch, idxTargetBufRow, pxPartWidPatchWid);

                idxPatch += 1;
            }
        }

        // if some data (< page size) still in buffers, force flush
        if (get_fc_buffer_sz() > 0)
        {
            pr_info("FC_BUFFERS still not empty but the image_dispatch is ended, "
                    "force flush");
            flush_img_fc_buffer(true);
        }

        free(matPatch.base);
        free(matPatchRow.base);
    }
    else
        pr_error("Unexpected PlanarConfig: %u", cfgPlanar);

    _TIFFfree(buf);
    TIFFClose(tif);
}

/* -------------------------------------------------------------------------- */
/*                    implementation of internal functions                    */
/* -------------------------------------------------------------------------- */

void dispatch_patch(size_t iPatch, const ByteMatrix_t *matPatch, size_t pxHeight, size_t pxWidth)
{
    // make sure both the blk row width and height are 4N
    const size_t bytesPatchWidth  = pxWidth * BYTES_PER_PIXEL;
    const size_t pxBlkBufWidth    = ALIGN_UP(pxWidth, PX_BLK_WIDTH);
    const size_t bytesBlkBufWidth = pxBlkBufWidth * BYTES_PER_PIXEL;

    // debug info, check whether the patch is full patch or not
    if (pxHeight != PX_PATCH_HEIGHT || pxWidth != PX_PATCH_WIDTH)
        pr_info("Patch[%lu] is a partial patch (H=%lu,W=%lu)", iPatch, pxHeight, pxWidth);

    assert_exit(pxBlkBufWidth <= PX_PATCH_WIDTH, "Unexpected BlockRow Width %lu", pxBlkBufWidth);

    // allocate buffer for a block and block row
    ByteMatrix_t matBlockRow = INIT_BYTE_MATRIX(PX_BLK_HEIGHT, bytesBlkBufWidth);
    assert_exit(matBlockRow.base, "Failed to allocate memory for ByteMatrix");

    ARRAY_FROM_BYTE_MATRIX(blockRow, matBlockRow);
    ARRAY_FROM_BYTE_MATRIX(patch, *matPatch);

    // handle patch (flush blockRow by blockRow)
    size_t idxTargetBufRow = 0;
    for (size_t iRow = 0; iRow < pxHeight; ++iRow)
    {
        // copy the whole row to target row of blockRow buffer
        memcpy((*blockRow)[idxTargetBufRow], (*patch)[iRow], bytesPatchWidth);
        idxTargetBufRow += 1;

        // blockRow buffer full, flush block by block (row major)
        if (idxTargetBufRow == 4)
        {
            idxTargetBufRow = 0;
            dispatch_blk_row(&matBlockRow, pxBlkBufWidth);
            memset_mat(&matBlockRow, 0, bytesBlkBufWidth); // clear blk buffer
        }
    }

    // if partial height blockRow exists, do padding and flush
    if (idxTargetBufRow > 0)
    {
        // padding zeros to invalid rows
        for (; idxTargetBufRow < PX_BLK_HEIGHT; ++idxTargetBufRow)
            memset((*blockRow)[idxTargetBufRow], 0, bytesBlkBufWidth);

        // flush partial height blockRow
        dispatch_blk_row(&matBlockRow, pxBlkBufWidth);
    }

    free(matBlockRow.base);
}

void dispatch_blk_row(const ByteMatrix_t *matBlkRow, size_t pxWidth)
{
    assert_exit(pxWidth % PX_BLK_WIDTH == 0, "BlockRow width should align to 4");

    ARRAY_FROM_BYTE_MATRIX(blkRow, *matBlkRow);
    for (size_t iPx = 0, iByte = 0; iPx < pxWidth; iPx += PX_BLK_WIDTH)
    {
        for (size_t iRow = 0; iRow < PX_BLK_HEIGHT; ++iRow)
        {
            // R
            FC_BUFFERS[iRow][FC_BUFFER_SZ + 0]                 = (*blkRow)[iRow][iByte + 0];
            FC_BUFFERS[iRow + PX_BLK_HEIGHT][FC_BUFFER_SZ + 0] = (*blkRow)[iRow][iByte + 0 + 6];
            FC_BUFFERS[iRow][FC_BUFFER_SZ + 1]                 = (*blkRow)[iRow][iByte + 3];
            FC_BUFFERS[iRow + PX_BLK_HEIGHT][FC_BUFFER_SZ + 1] = (*blkRow)[iRow][iByte + 3 + 6];

            // G
            FC_BUFFERS[iRow][FC_BUFFER_SZ + 2]                 = (*blkRow)[iRow][iByte + 1];
            FC_BUFFERS[iRow + PX_BLK_HEIGHT][FC_BUFFER_SZ + 2] = (*blkRow)[iRow][iByte + 1 + 6];
            FC_BUFFERS[iRow][FC_BUFFER_SZ + 3]                 = (*blkRow)[iRow][iByte + 4];
            FC_BUFFERS[iRow + PX_BLK_HEIGHT][FC_BUFFER_SZ + 3] = (*blkRow)[iRow][iByte + 4 + 6];

            // B
            FC_BUFFERS[iRow][FC_BUFFER_SZ + 4]                 = (*blkRow)[iRow][iByte + 2];
            FC_BUFFERS[iRow + PX_BLK_HEIGHT][FC_BUFFER_SZ + 4] = (*blkRow)[iRow][iByte + 2 + 6];
            FC_BUFFERS[iRow][FC_BUFFER_SZ + 5]                 = (*blkRow)[iRow][iByte + 5];
            FC_BUFFERS[iRow + PX_BLK_HEIGHT][FC_BUFFER_SZ + 5] = (*blkRow)[iRow][iByte + 5 + 6];
        }

        FC_BUFFER_SZ += 6;
        iByte += PX_BLK_WIDTH * BYTES_PER_PIXEL;
    }

    // flush buffer and
    flush_img_fc_buffer(false);
    pr_debug("Truncate FC_BUFFERS to %lu bytes", FC_BUFFER_SZ);
}

void flush_img_fc_buffer(bool force_flush)
{
    size_t bytesFlushedPerFC = 0;
    while (FC_BUFFER_SZ >= BYTES_PER_PAGE)
    {
        // flush 1 page to each FC
        for (size_t iFC = 0; iFC < NUM_FLASH_CHANNELS; ++iFC)
            flush_page_image(iFC, &FC_BUFFERS[iFC][bytesFlushedPerFC]);

        FC_BUFFER_SZ -= BYTES_PER_PAGE;
        bytesFlushedPerFC += BYTES_PER_PAGE;
    }

    // if force flush, padding and flush
    if (force_flush)
    {
        // flush 1 page to each FC
        for (size_t iFC = 0; iFC < NUM_FLASH_CHANNELS; ++iFC)
            flush_page_image(iFC, &FC_BUFFERS[iFC][bytesFlushedPerFC]);

        FC_BUFFER_SZ = 0;
    }
    else
    {
        // move data to begining
        uint8_t tmpValid[FC_BUFFER_SZ];
        for (size_t iFC = 0; iFC < NUM_FLASH_CHANNELS; ++iFC)
        {
            memcpy(tmpValid, &FC_BUFFERS[iFC][bytesFlushedPerFC], FC_BUFFER_SZ);
            memcpy(FC_BUFFERS[iFC], tmpValid, FC_BUFFER_SZ);
        }
    }
}
