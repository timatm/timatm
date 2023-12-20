#ifndef __NMC_HOST_PLUGIN_NVME_NMC_H__
#define __NMC_HOST_PLUGIN_NVME_NMC_H__

#include <stdint.h>
#include <stdbool.h>
#include "flash_config.h"

typedef struct
{
    // host info
    struct
    {
        int argc;
        char **argv;

        bool dry;
        char *data_file;
        struct nvme_dev *dev;

        uint8_t flags;
        uint16_t rsvd;
        uint32_t result;
        uint32_t timeout_ms;

        char *data;
        uint32_t data_len;
        char *metadata;
        uint32_t metadata_len;

        uint16_t iCh, iWay, iDie, iBlk, iPage;
    };

    // device (NVMe) info
    struct
    {
        union
        {
            struct
            {
                uint8_t OPCODE;
                uint8_t FUSE : 2;      // fuse operation
                uint8_t reserved0 : 4; //
                uint8_t PSDT : 2;      // 00 for PRP, 11 reserved, otherwise SGL.
                uint16_t CID;          // command id
            };
            uint32_t cdw00; // CDW 0: command info
        };

        union
        {
            uint32_t NSID;
            uint32_t cdw01; // CDW 1: namespace id
        };

        uint32_t cdw02; // command specific
        uint32_t cdw03; // command specific

        union
        {
            uint64_t meta_addr;
            struct
            {
                uint32_t cdw04;
                uint32_t cdw05;
            }; // CDW 4,5: dword aligned physical address of the metadata buffer
        };

        union
        {
            // struct SGL; // not supported yet
            struct
            {
                uint64_t PRP1; // physical address of the data buffer or PRP list
                uint64_t PRP2;
            };
            struct
            {
                uint32_t cdw06;
                uint32_t cdw07;
                uint32_t cdw08;
                uint32_t cdw09;
            };
        }; // CDW 6:9: data buffer info

        union
        {
            struct
            {
                union
                {
                    uint32_t cdw10;        // command specific
                    uint32_t monitor_mode; // command specific
                };
                uint32_t cdw11; // command specific
            };
            uint64_t slba; // the starting LBA
        };

        union
        {
            uint32_t cdw12; // command specific
            struct
            {
                uint16_t nlb; // number of logical blocks
                uint16_t unused;
            };
        };

        uint32_t cdw13; // command specific
        union
        {
            uint32_t nmc_new_mapping_filetype;
            uint32_t cdw14; // command specific
        };
        union
        {
            uint32_t nmc_new_mapping_nblks;
            uint32_t cdw15; // command specific
        };
    };
} nmc_config_t;

/* -------------------------------------------------------------------------- */
/*                             NMC related configs                            */
/* -------------------------------------------------------------------------- */

typedef enum _NMC_FILE_TYPES
{
    NMC_FILE_TYPE_NONE       = 0,
    NMC_FILE_TYPE_MODEL_UNET = 1,
    NMC_FILE_TYPE_IMAGE_TIFF = 2,
} NMC_FILE_TYPES;

typedef enum _NMC_STATUS_CODES
{
    NMC_SC_SUCCESS                             = 0x0700,
    NMC_SC_MAPPING_DISABLED                    = 0x0701,
    NMC_SC_MAPPING_REOPENED                    = 0x0702,
    NMC_SC_MAPPING_RECLOSED                    = 0x0703,
    NMC_SC_MAPPING_FILENAME_TOO_LONG           = 0x0704,
    NMC_SC_MAPPING_FILENAME_UNSUPPORTED        = 0x0705,
    SC_VENDOR_NMC_MAPPING_REGISTER_INIT_FAILED = 0x0706,
} NMC_STATUS_CODES;

#define NMC_FILENAME_MAX_BYTES 256

//       NMC  X X Packet X
//         \   \ \   \  /   Wr Rd
// bit: 7 | 6  5  4  3  2 | 1  0 |  hex  | description
//      1 | 1  0  0  0  0 | 0  1 |  C1h  | create mapping table for new file
//      1 | 1  0  0  0  0 | 1  0 |  C2h  | close mapping table
//      1 | 1  0  0  0  0 | 1  1 |  C3h  | inference the specified file
//      1 | 1  0  0  1  0 | 0  1 |  C9h  | write nmc packet

#define IO_NVM_NMC_ALLOC     0xC1 // create mapping table for new file
#define IO_NVM_NMC_FLUSH     0xC2 // close mapping table
#define IO_NVM_NMC_INFERENCE 0xC3 // inference the specified file
#define IO_NVM_NMC_INFERENCE_READ 0xC8
#define IO_NVM_NMC_WRITE     0xC9 // write packet (distribute to all FCs)

/* -------------------------------------------------------------------------- */
/*                              public interfaces                             */
/* -------------------------------------------------------------------------- */

int nmc_new_mapping(nmc_config_t config, uint32_t filetype, uint32_t nblks);
int nmc_close_mapping(nmc_config_t config);
int nmc_flush_packet(nmc_config_t *config, const uint8_t *buf, uint32_t sz);

int nmc_send_passthru(bool io_cmd, nmc_config_t config);

#endif /* __NMC_HOST_PLUGIN_NVME_NMC_H__ */