import sys
import math
import numpy as np
from PIL import Image
from pprint import pprint as pp

import logging
import traceback
from typing import *

# flash attributes
BYTES_PER_PAGE = 16384

NUM_FLASH_CHANNELS = 8
NUM_ROWS_PER_BLK = 4

DEST_CH_TABLE = [[0, 4], [1, 5], [2, 6], [3, 7]]

# image attributes
SAMPLE_IMG_PATH = "../data/nmc-part-modified.tiff"

NUM_CHANNELS_PER_PIXEL = 3
BYTES_PER_PIXEL = NUM_CHANNELS_PER_PIXEL

PX_PATCH_WIDTH = 512
PX_PATCH_HEIGHT = PX_PATCH_WIDTH
PX_PER_PATCH = PX_PATCH_WIDTH * PX_PATCH_HEIGHT
BYTES_PER_PATCH = PX_PER_PATCH * BYTES_PER_PIXEL

BYTES_PATCH_WIDTH = PX_PATCH_WIDTH * NUM_CHANNELS_PER_PIXEL
BYTES_PATCH_HEIGHT = PX_PATCH_HEIGHT * NUM_CHANNELS_PER_PIXEL

NUM_STEPS_PER_STEP_GRP = NUM_CHANNELS_PER_PIXEL  # RR policy
NUM_STEPS_ON_BLK_X = 2
NUM_STEPS_ON_BLK_Y = 2

PX_STEP_WIDTH = 2
PX_STEP_HEIGHT = 1

PX_BLK_WIDTH = PX_STEP_WIDTH * NUM_STEPS_ON_BLK_X
PX_BLK_HEIGHT = NUM_ROWS_PER_BLK
BYTES_PER_BLK = PX_BLK_WIDTH * PX_BLK_HEIGHT * BYTES_PER_PIXEL

PADDING_DONT_CARE = 0

# Image Geometry: (ignore overlapped patches)
#
#                 |---- Image Width ----|
#               - P(0,0) P(0,1) ... P(0,W) ---> Patch Row #0
#               | P(1,0) P(1,1) ... P(1,W)
#  Image Height |  ...
#               - P(H,0) P(H,1) ... P(H,W) ---> Patch Row #H
#                    |                 |
#                    v                 v
#              Patch Col #0      Patch Col #W
#
# Access Patern of Patches => (0,0) (0,1) ... (1,0) (1,1) ... (H,0) ... (H,W)

# Patch Geometry:
#
#                 |---- Patch Width ----|
#               - B(0,0) B(0,1) ... B(0,W) ---> Block Row #0
#               | B(1,0) B(1,1) ... B(1,W)
#  Patch Height |  ...
#               - B(H,0) B(H,1) ... B(H,W) ---> Block Row #H
#                    |                 |
#                    v                 v
#              Block Col #0      Block Col #W
#
# Access Patern of Blocks => (0,0) (0,1) ... (1,0) (1,1) ... (H,0) ... (H,W)

NUM_FULL_BLKS_PER_BLK_ROW = PX_PATCH_WIDTH // PX_BLK_WIDTH
PX_PARTIAL_BLK_WIDTH = PX_PATCH_WIDTH % PX_BLK_WIDTH
NUM_BLKS_PER_BLK_ROW = NUM_FULL_BLKS_PER_BLK_ROW + (1 if PX_PARTIAL_BLK_WIDTH > 0 else 0)

NUM_FULL_BLKS_PER_BLK_COL = PX_PATCH_HEIGHT // PX_BLK_HEIGHT
PX_PARTIAL_BLK_HEIGHT = PX_PATCH_HEIGHT % PX_BLK_HEIGHT
NUM_BLKS_PER_BLK_COL = NUM_FULL_BLKS_PER_BLK_COL + (1 if PX_PARTIAL_BLK_HEIGHT > 0 else 0)

NUM_BLKS_PER_PATCH = NUM_BLKS_PER_BLK_ROW * NUM_BLKS_PER_BLK_COL


if (PX_PATCH_WIDTH == PX_PATCH_HEIGHT) and (PX_PATCH_WIDTH % 4 == 0):
    assert (NUM_BLKS_PER_BLK_ROW == NUM_BLKS_PER_BLK_COL == (PX_PATCH_WIDTH // 4))
    assert (PX_PARTIAL_BLK_WIDTH == PX_PARTIAL_BLK_HEIGHT == 0)


# Block Geometry: (4x4 area) // FIXME
#
#                |--- Block Width ---|
#              - S(0,0) S(0,1) ... S(0,W) ---> Step Row #0
# Block Height | S(1,0) S(1,1) ... S(1,W)
#              |  ...
#              - S(H,0) S(H,1) ... S(H,W) ---> Step Row #H
#                   |                 |
#                   v                 v
#             Step Col #0       Step Col #W
#
# Access Patern of Steps => (0,0) (0,1) ... (1,0) (1,1) ... (H,0) ... (H,W)
#

NUM_STEPS_ON_BLK_X = PX_BLK_WIDTH // PX_STEP_WIDTH
