from img_policy_contig_verify import *

import numpy as np
from ctypes import *
from pprint import pprint as pp


# ---------------------------------------------------------------------------- #
#                                 clear buffer                                 #
# ---------------------------------------------------------------------------- #

for iFC in range(NUM_FLASH_CHANNELS):
    open(f"logs/buffer.{iFC}.log", "w").close()

# ---------------------------------------------------------------------------- #
#                                     init                                     #
# ---------------------------------------------------------------------------- #

SO_PATH = "./img_placement_contig.so"
LIB = CDLL(SO_PATH)

# void dispatch_image(uint8_t *imgFlatten, size_t pxWidth, size_t pxHeight)
LIB.dispatch_image.argtypes = [np.ctypeslib.ndpointer(dtype=np.uint8), c_size_t, c_size_t]
LIB.dispatch_image.restype = None

image = np.random.randint(0, 256, (10000, 9999, 3), dtype=np.uint8)

imgHeight, imgWidth = image.shape[:2]
imgFlatten = image.reshape(-1)

# pp(image)
# dump_flatten_image(image)

# ---------------------------------------------------------------------------- #
#                              dispatch and verify                             #
# ---------------------------------------------------------------------------- #

LIB.dispatch_image(imgFlatten, imgWidth, imgHeight)
pp("dispatch_image finishes!")

np.save("verify.npy", image)

verify_image(image, False)
pp("verify_image finishes!")
