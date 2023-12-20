from img_policy_contig_header import *


def verify_block(imgPadded: np.ndarray, block: List[List[int]], iPage: int, offPage: int):

    # the given blk size should be (8,2)
    try:
        pxImgHeight, pxImgWidth = imgPadded.shape[:2]  # (H,W,C)\

        blk = np.array(block)
        assert (blk.shape == (8, 2)), f"Expect block shape (8,2), but got {blk.shape}"
        assert (offPage % 2 == 0), f"Expect page offset to be 2N, but got {offPage}"

        offFlashChannel = iPage * BYTES_PER_PAGE + offPage
        idxCurrentBlk = offFlashChannel // 6  # each RGB block distribute 6 bytes to each flash channel
        iByte = idxCurrentBlk * BYTES_PER_BLK  # the base offset of current block

        # make sure the buffer can be divided into 4x4 blks perfectly
        pxBufHeight = pxImgHeight + (0 if pxImgHeight % 4 == 0 else (4 - (pxImgHeight % 4)))
        pxBufWidth = pxImgWidth + (0 if pxImgWidth % 4 == 0 else (4 - (pxImgWidth % 4)))

        numFullWidthPatches = pxBufWidth // PX_PATCH_WIDTH
        pxPartialWidthPatchWidth = pxBufWidth % PX_PATCH_WIDTH

        numFullHeightPatches = pxBufHeight // PX_PATCH_HEIGHT
        pxPartialHeightPatchHeight = pxBufHeight % PX_PATCH_HEIGHT

        # locate current patch row
        bytesFullHeightPatchRow = pxBufWidth * PX_PATCH_HEIGHT * BYTES_PER_PIXEL
        idxPatchRow = iByte // bytesFullHeightPatchRow
        offPatchRow = iByte % bytesFullHeightPatchRow

        # locate current patch (need to check whether currently at last patch row)
        pxCurrentPatchHeight = PX_PATCH_HEIGHT if idxPatchRow < numFullHeightPatches else pxPartialHeightPatchHeight
        bytesFullWidthPatch = PX_PATCH_WIDTH * pxCurrentPatchHeight * BYTES_PER_PIXEL

        idxPatch = offPatchRow // bytesFullWidthPatch
        offPatch = offPatchRow % bytesFullWidthPatch

        # locate current block row (need to check whether currently at last patch row)
        pxCurrentPatchWidth = PX_PATCH_WIDTH if idxPatch < numFullWidthPatches else pxPartialWidthPatchWidth
        assert (pxCurrentPatchWidth % 4 == 0), f"Expect patch all width to be 4N, but got {pxCurrentPatchWidth}"

        numBlksPerBlkRow = pxCurrentPatchWidth // 4
        bytesCurrentBlkRow = numBlksPerBlkRow * BYTES_PER_BLK

        idxBlkRow = offPatch // bytesCurrentBlkRow
        offBlkRow = offPatch % bytesCurrentBlkRow

        # locate current block and img channel
        idxBlk = offBlkRow // BYTES_PER_BLK
        idxImgCh = (offFlashChannel // 2) % BYTES_PER_PIXEL

        pxBlkPosLeft = idxPatch * PX_PATCH_WIDTH + idxBlk * PX_BLK_WIDTH
        pxBlkPosTop = idxPatchRow * PX_PATCH_HEIGHT + idxBlkRow * PX_BLK_HEIGHT
        pxBlkPosRight = pxBlkPosLeft + 4
        pxBlkPosBottom = pxBlkPosTop + 4

        # get current block data (maybe out-of-range)
        ans = np.full((8, 2), PADDING_DONT_CARE, dtype=np.uint8)
        pxValidBlkWidth = 4 if pxBlkPosRight <= pxImgWidth else (4 - (pxBlkPosRight - pxImgWidth))
        pxValidBlkHeight = 4 if pxBlkPosBottom <= pxImgHeight else (4 - (pxBlkPosBottom - pxImgHeight))

        assert ((pxValidBlkWidth > 0) and (pxValidBlkHeight > 0))
        for iFC in range(NUM_FLASH_CHANNELS):
            # ignore padded rows
            if (iFC % 4) < pxValidBlkHeight:
                # ignore padded cols
                if pxValidBlkWidth == 1:
                    if iFC < 4:
                        ans[iFC][0] = imgPadded[pxBlkPosTop+iFC][pxBlkPosLeft][idxImgCh]
                    else:
                        continue
                elif pxValidBlkWidth == 2:
                    if iFC < 4:
                        ans[iFC][0] = imgPadded[pxBlkPosTop+iFC][pxBlkPosLeft][idxImgCh]
                        ans[iFC][1] = imgPadded[pxBlkPosTop+iFC][pxBlkPosLeft+1][idxImgCh]
                    else:
                        continue
                elif pxValidBlkWidth == 3:
                    if iFC < 4:
                        ans[iFC][0] = imgPadded[pxBlkPosTop+iFC][pxBlkPosLeft][idxImgCh]
                        ans[iFC][1] = imgPadded[pxBlkPosTop+iFC][pxBlkPosLeft+1][idxImgCh]
                    else:
                        ans[iFC][0] = imgPadded[pxBlkPosTop+(iFC % 4)][pxBlkPosLeft+2][idxImgCh]
                elif pxValidBlkWidth == 4:
                    if iFC < 4:
                        ans[iFC][0] = imgPadded[pxBlkPosTop+iFC][pxBlkPosLeft][idxImgCh]
                        ans[iFC][1] = imgPadded[pxBlkPosTop+iFC][pxBlkPosLeft+1][idxImgCh]
                    else:
                        ans[iFC][0] = imgPadded[pxBlkPosTop+(iFC % 4)][pxBlkPosLeft+2][idxImgCh]
                        ans[iFC][1] = imgPadded[pxBlkPosTop+(iFC % 4)][pxBlkPosLeft+3][idxImgCh]

        # compare with given block
        for iFC in range(NUM_FLASH_CHANNELS):
            for i in range(2):
                assert (ans[iFC][i] == blk[iFC][i]), f"Expect block data: \n{ans}\n, but got: \n{blk}"

        return True

    except Exception as ex:
        traceback.print_exception(type(ex), ex, ex.__traceback__)
        return False


def verify_image(image: np.ndarray, zero_padding: bool):

    if zero_padding:
        raise RuntimeError("Not implemented yet")

    pxImgHeight, pxImgWidth = image.shape[:2]
    pxBufHeight = pxImgHeight + (0 if pxImgHeight % 4 == 0 else (4-(pxImgHeight % 4)))
    pxBufWidth = pxImgWidth + (0 if pxImgWidth % 4 == 0 else (4-(pxImgWidth % 4)))

    buffers = [None] * NUM_FLASH_CHANNELS
    for iFC in range(NUM_FLASH_CHANNELS):
        buffers[iFC] = open(f"logs/buffer.{iFC}.log", 'r').readlines()

    nLines = len(buffers[0])
    for iFC in range(NUM_FLASH_CHANNELS):
        assert (nLines == len(buffers[iFC])), f"Expect FC[{iFC}] have {nLines} lines of data, but got {len(buffers[iFC])}"

    # parse data line by line (each line is a page)
    iImgCh = 0
    block = np.full((8, 2), PADDING_DONT_CARE, dtype=np.uint8)
    nValidBytes = (pxBufWidth * pxBufHeight * BYTES_PER_PIXEL) // NUM_FLASH_CHANNELS
    for iPage in range(nLines):

        # parse line data (int8s separated by spaces)
        pages = np.array([buffers[iFC][iPage].replace('\n', '').split(' ') for iFC in range(NUM_FLASH_CHANNELS)], dtype=np.uint8)

        for iFC in range(NUM_FLASH_CHANNELS):
            assert (BYTES_PER_PAGE == len(pages[iFC])), f"Assert ({BYTES_PER_PAGE} == {len(pages[iFC])}) failed"

        # the data in this page may not be all valid
        for iByte in range(0, min(BYTES_PER_PAGE, nValidBytes), 2):

            for iFC in range(NUM_FLASH_CHANNELS):
                block[iFC] = pages[iFC][iByte:iByte+2]

            assert verify_block(image, block, iPage, iByte), f"At Page[{iPage}]+{iByte}"

            # update img channel info
            iImgCh = (iImgCh + 1) % 3

        # update remaining valid blocks
        nValidBytes -= BYTES_PER_PAGE


def dump_flatten_image(image: np.ndarray):
    np.set_printoptions(threshold=sys.maxsize, linewidth=999)

    imgFlatten = [image[:, :, iImgCh] for iImgCh in range(BYTES_PER_PIXEL)]
    imgFlattenStr = np.array(imgFlatten, dtype=np.str_)
    imgFlattenStr = np.char.zfill(imgFlattenStr, 3)

    for iRow, _ in enumerate(imgFlatten[0]):
        for iImgCh, imgCh in enumerate("RGB"):
            pp(f"Row[{iRow}-{imgCh}]: {imgFlatten[iImgCh][iRow]}")
