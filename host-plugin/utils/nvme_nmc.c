#include "nvme.h"

#include "./nvme_nmc.h"
#include "./debug.h"

// #define NMC_SHORT_FILENAME true

/* -------------------------------------------------------------------------- */
/*                             internal functions                             */
/* -------------------------------------------------------------------------- */

static inline int nmc_send_io_passthru(nmc_config_t config)
{
    return nmc_send_passthru(true, config);
}
static void nmc_verify(nmc_config_t config);

/* -------------------------------------------------------------------------- */
/*                              public interfaces                             */
/* -------------------------------------------------------------------------- */

int nmc_new_mapping(nmc_config_t config, uint32_t filetype, uint32_t nblks)
{
    int err;
    uint32_t dsize = 0;
    void *dptr     = NULL;

#if (NMC_SHORT_FILENAME == true)
    for (int iCh = 0; iCh < 4; ++iCh)
        ((char *)&config.cdw10)[iCh] = config.data_file[iCh];
    pr_info("NMC_SHORT_FILENAME: '%s' -> 0x%X", config.data_file, config.cdw10);
#else
    // #pragma GCC error "The option for long filename needs to be fixed"
    dsize = NMC_FILENAME_MAX_BYTES;
    dptr  = aligned_alloc(getpagesize(), dsize);
    strncpy(dptr, config.data_file, dsize);
    pr_info("NMC_LONG_FILENAME: '%s' -> '%s'", config.data_file, (char *)dptr);
#endif

    config.OPCODE   = IO_NVM_NMC_ALLOC;
    config.data     = dptr;
    config.data_len = dsize;

    config.nmc_new_mapping_filetype = filetype;
    config.nmc_new_mapping_nblks    = nblks;

    err = nmc_send_io_passthru(config);

#if (NMC_SHORT_FILENAME == false)
    free(dptr);
#endif
    return err;
}

int nmc_close_mapping(nmc_config_t config)
{
    // implicitly flush the buffered data to ensure all data are persisted
    config.OPCODE = IO_NVM_NMC_FLUSH;
    return nmc_send_io_passthru(config);
}

int nmc_flush_packet(nmc_config_t *config, const uint8_t *buf, uint32_t sz)
{
    // create new config and inherit from global config
    nmc_config_t cfg = *config;

    // set data address, number
    cfg.OPCODE   = IO_NVM_NMC_WRITE;
    cfg.data     = buf;
    cfg.data_len = sz;
    cfg.nlb      = (sz + (BYTES_NVME_BLOCK - 1)) / BYTES_NVME_BLOCK;

    config->slba += cfg.nlb;
    --cfg.nlb; /* nlb is zero-based */

    // TODO: may need some additional info for physical placement

    int res = nmc_send_io_passthru(cfg);

#if (NMC_FLUSH_VERIFY == true)
    if (!config->dry)
        nmc_verify(cfg);
#endif

    return res;
}

int nmc_send_passthru(bool io_cmd, nmc_config_t config)
{
    int err = 0;

    if (config.dry)
    {
        pr("opcode       : 0x%02x", config.OPCODE);
        pr("nsid         : 0x%02x", config.NSID);
        pr("flags        : 0x%04x", config.flags);
        pr("rsvd         : 0x%08x", config.rsvd);
        pr("cdw2         : 0x%08x", config.cdw02);
        pr("cdw3         : 0x%08x", config.cdw03);
        pr("data_addr    : %p", config.data);
        pr("madata_addr  : %p", config.metadata);
        pr("data_len     : 0x%08x", config.data_len);
        pr("mdata_len    : 0x%08x", config.metadata_len);
        pr("slba         : 0x%08lx", config.slba);
        pr("nlb          : 0x%08x", config.nlb);
        pr("cdw10        : 0x%08x", config.cdw10);
        pr("cdw11        : 0x%08x", config.cdw11);
        pr("cdw12        : 0x%08x", config.cdw12);
        pr("cdw13        : 0x%08x", config.cdw13);
        pr("cdw14        : 0x%08x", config.cdw14);
        pr("cdw15        : 0x%08x", config.cdw15);
    }

    if (config.dry)
        return err;

    if (io_cmd)
    {
        err = nvme_io_passthru(dev_fd(config.dev), config.OPCODE, config.flags, config.rsvd,
                               config.NSID, config.cdw02, config.cdw03, config.cdw10, config.cdw11,
                               config.cdw12, config.cdw13, config.cdw14, config.cdw15,
                               config.data_len, config.data, config.metadata_len, config.metadata,
                               config.timeout_ms, &config.result);
        switch (err)
        {
        case NMC_SC_SUCCESS:
        case 0: // success
            break;
        case NMC_SC_MAPPING_REOPENED:
            pr_error("NMC Mapping Reopened");
            break;
        case NMC_SC_MAPPING_RECLOSED:
            pr_error("NMC Mapping Reclosed");
            break;
        case NMC_SC_MAPPING_FILENAME_TOO_LONG:
            pr_error("NMC Mapping Filename Too Long");
            break;
        case NMC_SC_MAPPING_FILENAME_UNSUPPORTED:
            pr_error("NMC Mapping Filename Contains Unsupported Characters");
            break;
        case SC_VENDOR_NMC_MAPPING_REGISTER_INIT_FAILED:
            pr_error("NMC Mapping Register Initialization Failed");
            break;
        case NMC_SC_MAPPING_DISABLED:
            pr_error("NMC Mapping is Disabled");
            break;
        default:
            pr_error("Request failed (err,res=%d,%u): %s", err, config.result, nvme_strerror(err));
            break;
        }
    }
    else
    {
        err = nvme_admin_passthru(dev_fd(config.dev), config.OPCODE, config.flags, config.rsvd,
                                  config.NSID, config.cdw02, config.cdw03, config.cdw10,
                                  config.cdw11, config.cdw12, config.cdw13, config.cdw14,
                                  config.cdw15, config.data_len, config.data, config.metadata_len,
                                  config.metadata, config.timeout_ms, &config.result);

        switch (err)
        {
        case 0: // success
            break;
        default:
            pr_error("Request failed (err,res=%d,%u): %s", err, config.result, nvme_strerror(err));
            break;
        }
    }

    return err;
}

/* -------------------------------------------------------------------------- */
/*                             internal utilities                             */
/* -------------------------------------------------------------------------- */

static void nmc_verify(nmc_config_t config)
{
    const char *origin = config.data;

    // alloc space for read command
    config.OPCODE = 0x02;
    config.data   = aligned_alloc(getpagesize(), BYTES_PACKET);
    memset(config.data, 0, BYTES_PACKET);

    // read the written data into new buffer
    int err = nmc_send_io_passthru(config);

    // compare with original data if the read operations succeeded
    if (err)
        pr("%s failed (%d): %s\n", __func__, err, nvme_strerror(err));
    else
        for (size_t iByte = 0; iByte < BYTES_PACKET; ++iByte)
            assert_exit(origin[iByte] == config.data[iByte], "failed at %lu byte", iByte);

    free(config.data);
}