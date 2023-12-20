#ifndef __NMC_HOST_PLUGIN_FLASH_CONFIG_H__
#define __NMC_HOST_PLUGIN_FLASH_CONFIG_H__

#include <stdint.h>
#include <stdbool.h>

#define OPENSSD_NSID 120730624

#define BYTES_NVME_BLOCK 4096
#define BYTES_INF_RESULT 16384

/* -------------------------------------------------------------------------- */
/*                            flash memory geometry                           */
/* -------------------------------------------------------------------------- */

#define NUM_CHANNELS       8
#define NUM_FLASH_CHANNELS (NUM_CHANNELS)

#define BYTES_PAGE_SIZE 16384
#define BYTES_PACKET    (BYTES_PAGE_SIZE * NUM_FLASH_CHANNELS)

#define NUM_PAGES_PER_BLOCK 256

#define VDIE2PCH(dieNo)  ((dieNo) % (NUM_CHANNELS))
#define VDIE2PWAY(dieNo) ((dieNo) / (NUM_CHANNELS))

/* -------------------------------------------------------------------------- */
/*                                  bandwidth                                 */
/* -------------------------------------------------------------------------- */

#define BYTES_PER_PAGE         (BYTES_PAGE_SIZE)
#define BYTES_PER_CYCLE_PER_FC 2
#define BYTES_PER_CYCLE        (NUM_FLASH_CHANNELS * BYTES_PER_CYCLE_PER_FC)

#endif /* __NMC_HOST_PLUGIN_FLASH_CONFIG_H__ */