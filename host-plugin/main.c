#include "nvme.h"

#define CREATE_CMD
#include "main.h"

#include <stddef.h>
#include <string.h>
#include <sys/mman.h> /* for munmap(2) */
#include <fcntl.h>    /* for O_* flags */

#include "tiffio.h"

#include "utils/nvme_nmc.h"
#include "utils/onnx/model_parser.h"
#include "utils/placement/img_policy_contig.h"
#include "utils/placement/model_policy_rr.h"

#include "utils/debug.h"

/* -------------------------------------------------------------------------- */
/*                          user defined flush logics                         */
/* -------------------------------------------------------------------------- */

// #define USER_FLUSH_IMAGE         true
// #define USER_MODEL_DATA_APPENDER true

static nmc_config_t cfgNMCWrite;

static size_t numPackets   = 0; // for debugging
static uint8_t *bufPacket  = NULL;
static uint8_t idxTargetFC = 0;

static void flush_page_to_nand(uint8_t iFC, uint8_t *data)
{
    uint8_t *bufTargetPacket = &bufPacket[iFC * BYTES_PER_PAGE];
    assert_exit((idxTargetFC == iFC), "The expected target FC is %u, not %u", idxTargetFC, iFC);
    memcpy(bufTargetPacket, data, BYTES_PAGE_SIZE);

    idxTargetFC += 1;
    if (idxTargetFC == NUM_FLASH_CHANNELS)
    {
        idxTargetFC = 0;

        // dump data to files (in binary format) for verification
#if (NMC_FLUSH_VERIFY == true)
        for (uint8_t iFC = 0; iFC < NUM_FLASH_CHANNELS; ++iFC)
            _flush_page_to_file_bin(iFC, &bufPacket[BYTES_PER_PAGE * iFC], "logs/buffer-data");
#endif

        // flush to flash memory (slba will be updated)
        nmc_flush_packet(&cfgNMCWrite, bufPacket, BYTES_PACKET);

        pr_debug("Packet[%lu] flushed!", numPackets);
        ++numPackets; // do not merge into pr_debug, or assert will failed
    }
}

#if (USER_FLUSH_IMAGE == true)
void flush_page_image(uint8_t iFC, uint8_t *data) { flush_page_to_nand(iFC, data); }
#endif /* USER_FLUSH_IMAGE */

#if (USER_FLUSH_MODEL == true)
void flush_page_model(uint8_t iFC, uint8_t *data)
{
    pr_debug("FC[%u]: flush model buffer", iFC);
    _flush_page_to_file_bin(iFC, data, "logs/model-buffer");
    flush_page_to_nand(iFC, data);
}
#endif /* USER_FLUSH_MODEL */

#if (USER_MODEL_DATA_APPENDER == true)
void append_model_data(uint8_t data, bool end) { model_data_appender_rr(data, end); }
#endif /* USER_MODEL_DATA_APPENDER */

#if (USER_MODEL_DATA_FLUSH_ALL == true)
void flush_all_model_data() { model_data_force_flush_all(); }
#endif /* USER_MODEL_DATA_FLUSH_ALL */

/* -------------------------------------------------------------------------- */
/*                              plugin functions                              */
/* -------------------------------------------------------------------------- */
static int inference_read(int argc, char **argv, struct command *cmd, struct plugin * )
{
    pr_info("inference read start");
    // write the data buffer
    int err;
    nmc_config_t config = {.argc = argc, .argv = argv, .NSID = OPENSSD_NSID};

    OPT_ARGS(opts) = {
        OPT_STR("file", 'f', &config.data_file, "path to file"),
        OPT_FLAG("dry-run", 'd', &config.dry, "execute without writing data to device"), OPT_END()};

    err = parse_and_open(&config.dev, config.argc, config.argv, "nmc-flush-buffer", opts);
    assert_return(!err, err, "`parse_and_open()` failed...");

    // create sample buffer
    config.data_len = BYTES_INF_RESULT*4;
    config.data     = aligned_alloc(getpagesize(), config.data_len);
    assert_return(config.data, errno, "failed to allocate data buffer...");

    // check filename length
    //assert_return((strlen(config.data_file) <= config.data_len), -EINVAL, "filename too long...");

    // fill the filename into data buffer
    //pr("fill the filename \"%s\" into data buffer", config.data_file);
    //memcpy(config.data, config.data_file, strlen(config.data_file));

    // send request
    config.OPCODE    = IO_NVM_NMC_INFERENCE_READ;
    config.PSDT      = 0; /* use PRP */
    config.meta_addr = (uintptr_t)NULL;
    config.PRP1      = (uintptr_t)config.data;

    if (config.dry)
    {
        pr("opcode       : 0x%02x", config.OPCODE);
        pr("nsid         : 0x%02x", config.NSID);
        pr("cdw2         : 0x%08x", config.cdw02);
        pr("cdw3         : 0x%08x", config.cdw02);
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

    err =
        nvme_io_passthru(dev_fd(config.dev), config.OPCODE, config.flags, config.rsvd, config.NSID,
                         config.cdw02, config.cdw03, config.cdw10, config.cdw11, config.cdw12,
                         config.cdw13, config.cdw14, config.cdw15, config.data_len, config.data,
                         config.metadata_len, config.metadata, config.timeout_ms, &config.result);
    if (err)
        pr_error("nvme_io_passthru returned: %d (%s)", err, nvme_strerror(err));

    if (config.data){
        if (!err && config.data_file)
            _flush_page_to_file_bin_noC(config.data, config.data_file);
        free(config.data);
    }
    // free resources
    
    return err;
}


static int inference(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    // write the data buffer
    int err;
    nmc_config_t config = {.argc = argc, .argv = argv, .NSID = OPENSSD_NSID};

    OPT_ARGS(opts) = {
        OPT_STR("file", 'f', &config.data_file, "the filename of the image to inference"),
        OPT_FLAG("dry-run", 'd', &config.dry, "execute without writing data to device"), OPT_END()};

    err = parse_and_open(&config.dev, config.argc, config.argv, "nmc-flush-buffer", opts);
    assert_return(!err, err, "`parse_and_open()` failed...");

    // create sample buffer
    config.data_len = BYTES_NVME_BLOCK;
    config.data     = aligned_alloc(getpagesize(), config.data_len);
    assert_return(config.data, errno, "failed to allocate data buffer...");

    

    // check filename length
    assert_return((strlen(config.data_file) <= config.data_len), -EINVAL, "filename too long...");

    // fill the filename into data buffer
    pr("fill the filename \"%s\" into data buffer", config.data_file);
    memcpy(config.data, config.data_file, strlen(config.data_file));

    // send request
    config.OPCODE    = IO_NVM_NMC_INFERENCE;
    config.PSDT      = 0; /* use PRP */
    config.meta_addr = (uintptr_t)NULL;
    config.PRP1      = (uintptr_t)config.data;

    if (config.dry)
    {
        pr("opcode       : 0x%02x", config.OPCODE);
        pr("nsid         : 0x%02x", config.NSID);
        pr("cdw2         : 0x%08x", config.cdw02);
        pr("cdw3         : 0x%08x", config.cdw02);
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

    err =
        nvme_io_passthru(dev_fd(config.dev), config.OPCODE, config.flags, config.rsvd, config.NSID,
                         config.cdw02, config.cdw03, config.cdw10, config.cdw11, config.cdw12,
                         config.cdw13, config.cdw14, config.cdw15, config.data_len, config.data,
                         config.metadata_len, config.metadata, config.timeout_ms, &config.result);
    printf("%x",err);
    if (err==0){
        inference_read(argc,argv,cmd,plugin);
    }
    else{
        pr_error("nvme_io_passthru returned: %d (%s)", err, nvme_strerror(err));
    }
        

    // free resources
    free(config.data);
    return err;
}

extern int parse_onnx_unet(const Onnx__ModelProto *model, const ONNX_LAYER_t *layers, size_t n);
static int write_model(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    cfgNMCWrite = (nmc_config_t){.argc = argc, .argv = argv, .NSID = OPENSSD_NSID};

    // the buffer used by NVMe comand should be aligned to dram page size
    bufPacket = aligned_alloc(getpagesize(), BYTES_PACKET);

    OPT_ARGS(opts) = {
        OPT_FILE("data-file", 'f', &cfgNMCWrite.data_file, "the path to the model file"),
        OPT_FLAG("dry-run", 'd', &cfgNMCWrite.dry, "execute without writing data to device"),
        OPT_END()};

    // try to open target nvme dev
    int err = parse_and_open(&cfgNMCWrite.dev, argc, argv, "write-model", opts);
    assert_return(!err, err, "`parse_and_open()` failed...");
    assert_return(cfgNMCWrite.data_file != NULL, -1, "Target model file not specified...");

    // FIXME: not able to expect model size without parsing
    uint32_t nPacketsExpected = 255;

    // try to allocate new mapping table
    err = nmc_new_mapping(cfgNMCWrite, NMC_FILE_TYPE_MODEL_UNET, nPacketsExpected);
    assert_exit(err == 0, "Failed to allocate NMC mapping table");

    // parse model file
    Onnx__ModelProto *model = try_unpack_onnx(cfgNMCWrite.data_file);
    if (model)
    {
        parse_onnx_unet(model, NULL, 0);
        assert_exit(numPackets <= nPacketsExpected, "Expect < %u, but flush %lu packets",
                    nPacketsExpected, numPackets);
        onnx__model_proto__free_unpacked(model, NULL);
    }
    else
        pr("Failed to unpack onnx file...");

    // release resources
    err = nmc_close_mapping(cfgNMCWrite);
    assert_exit(err == 0, "Failed to close NMC mapping table");
    free(bufPacket);
    return 0;
}

static int write_tiff(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    cfgNMCWrite = (nmc_config_t){.argc = argc, .argv = argv, .NSID = OPENSSD_NSID};

    // the buffer used by NVMe comand should be aligned to dram page size
    bufPacket = aligned_alloc(getpagesize(), BYTES_PACKET);

    OPT_ARGS(opts) = {
        OPT_SUFFIX("slba", 's', &cfgNMCWrite.slba, "starting lba"),
        OPT_FILE("data-file", 'f', &cfgNMCWrite.data_file, "the path of tiff file"),
        OPT_FLAG("dry-run", 'd', &cfgNMCWrite.dry, "execute without writing data to device"),
        OPT_END()};

    // try to open target nvme dev
    int err = parse_and_open(&cfgNMCWrite.dev, argc, argv, "write-tiff", opts);
    assert_return(!err, err, "`parse_and_open()` failed...");
    assert_return(cfgNMCWrite.data_file != NULL, -1, "Target tiff image not specified...");

    // try to open tiff file and get image size for calc nblks
    uint32_t pxHeight, pxWidth;

    TIFF *tif = TIFFOpen(cfgNMCWrite.data_file, "r");
    assert_exit(tif != NULL, "Failed to open TIFF file '%s'", cfgNMCWrite.data_file);
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &pxWidth);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &pxHeight);
    TIFFClose(tif);

    // calc number of blocks needed by this image
    uint32_t nbytes   = pxHeight * pxWidth * BYTES_PER_PIXEL;
    uint32_t npages   = (nbytes + (BYTES_PER_PAGE - 1)) / BYTES_PER_PAGE;
    uint32_t npackets = (npages + (NUM_FLASH_CHANNELS - 1)) / NUM_FLASH_CHANNELS;
    uint32_t nblks    = (npackets + (NUM_PAGES_PER_BLOCK - 1)) / NUM_PAGES_PER_BLOCK;

    err = nmc_new_mapping(cfgNMCWrite, NMC_FILE_TYPE_IMAGE_TIFF, nblks);
    assert_exit(err == 0, "Failed to allocate NMC mapping table");

    dispatch_tiff(cfgNMCWrite.data_file);
    assert_exit(npackets == numPackets, "Expect %u, but flush %lu packets", npackets, numPackets);

    err = nmc_close_mapping(cfgNMCWrite);
    assert_exit(err == 0, "Failed to close NMC mapping table");

    free(bufPacket);
    return 0;
}

/* -------------------------------------------------------------------------- */
/*                             debugging functions                            */
/* -------------------------------------------------------------------------- */

typedef struct
{
    char *mode_str;
    uint32_t mode_code;
} monitor_mode_t;

// #define MONITOR_MODE_OPT_NSID(ptr) OPT_UINT("nsid", 'n', ptr, "Namespace ID")
#define MONITOR_MODE_OPT_DRY(ptr)   OPT_FLAG("dry", 'd', ptr, "Stop before sending to device")
#define MONITOR_MODE_OPT_FORCE(ptr) OPT_FLAG("force", 'f', ptr, "Force")

#define MONITOR_MODE_OPT_IDX(ptr) OPT_UINT("idx", 'i', ptr, "Index")
#define MONITOR_MODE_OPT_CNT(ptr) OPT_UINT("cnt", 'c', ptr, "Count")

#define MONITOR_MODE_OPT_FID(ptr)   OPT_UINT("fid", 'f', ptr, "Predefined file ID")
#define MONITOR_MODE_OPT_FTYPE(ptr) OPT_UINT("filetype", 't', ptr, "Type of NMC file")
#define MONITOR_MODE_OPT_FILE(ptr)  OPT_FILE("file", 'f', ptr, "Path to file")

#define MONITOR_MODE_OPT_IDIE(ptr)  OPT_UINT("die", 'c', ptr, "Index of flash die")
#define MONITOR_MODE_OPT_IBLK(ptr)  OPT_UINT("block", 'b', ptr, "Index of flash block")
#define MONITOR_MODE_OPT_IPAGE(ptr) OPT_UINT("page", 'p', ptr, "Index of flash page")

#define MONITOR_MODE_OPT_ROW(ptr)  OPT_UINT("row", 'r', ptr, "Row address of a flash page")
#define MONITOR_MODE_OPT_VBA(ptr)  OPT_UINT("vba", 'v', ptr, "Address of virtual block")
#define MONITOR_MODE_OPT_LBA(ptr)  OPT_UINT("lba", 'l', ptr, "Address of logical block")
#define MONITOR_MODE_OPT_SLBA(ptr) OPT_UINT("slba", 's', ptr, "Start address of logical block")
#define MONITOR_MODE_OPT_ELBA(ptr) OPT_UINT("elba", 'e', ptr, "End address of logical block")

#define MONITOR_MODE_OPT_LSA(ptr) OPT_UINT("lsa", 'l', ptr, "Address of logical slice")
#define MONITOR_MODE_OPT_VSA(ptr) OPT_UINT("vsa", 'v', ptr, "Address of virtual slice")

#define VERIFY_SET_MONITOR_MODE_OPTS(gOpts, modeOpts)                                              \
    ({                                                                                             \
        assert_return(sizeof(modeOpts) < sizeof(gOpts), -1, "Too many options...");                \
        memcpy(gOpts, modeOpts, sizeof(modeOpts));                                                 \
    })

#define ADMIN_MONITOR_BUFFER      0xC1
#define ADMIN_MONITOR_MAPPING     0xC3
#define ADMIN_MONITOR_FLASH       0xC5
#define ADMIN_MONITOR_NMC_MAPPING 0xD1

static int monitor_nmc_mapping(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    // define subcommands
    const char *mode = (argv[1] && argv[2]) ? argv[2] : "(null)"; // subcmd dev mode opts

    const monitor_mode_t MODE_RECORD_BLOCK     = {"record-blk", 1};
    const monitor_mode_t MODE_RECOVER_MAPPING  = {"recover-map", 2};
    const monitor_mode_t MODE_SEARCH_FILE_ID   = {"search-fid", 3};
    const monitor_mode_t MODE_SEARCH_ENTRY_IDX = {"search-entry-idx", 4};
    const monitor_mode_t MODE_FILE_ALLOC       = {"alloc", 5};
    const monitor_mode_t MODE_FILE_FREE        = {"free", 6};

    const monitor_mode_t MONITOR_MODES[] = {
        MODE_RECORD_BLOCK,     MODE_RECOVER_MAPPING, MODE_SEARCH_FILE_ID,
        MODE_SEARCH_ENTRY_IDX, MODE_FILE_ALLOC,      MODE_FILE_FREE,
    };

    // check subcommand and setup options
    int err;
    bool io_cmd = false;

    nmc_config_t config = {.argc = argc, .argv = argv, .OPCODE = ADMIN_MONITOR_NMC_MAPPING};
    OPT_ARGS(opts)      = {[0 ... 20] = OPT_END()};

    if (!strcmp(mode, MODE_RECORD_BLOCK.mode_str))
    {
        config.monitor_mode = MODE_RECORD_BLOCK.mode_code;

        OPT_ARGS(cmdOpts) = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_IDIE(&config.cdw11),
            MONITOR_MODE_OPT_VBA(&config.cdw12),
            OPT_END(),
        };

        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_RECOVER_MAPPING.mode_str))
    {
        config.monitor_mode = MODE_RECOVER_MAPPING.mode_code;

        OPT_ARGS(cmdOpts) = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            OPT_END(),
        };

        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_SEARCH_FILE_ID.mode_str))
    {
        config.monitor_mode = MODE_SEARCH_FILE_ID.mode_code;

        OPT_ARGS(cmdOpts) = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_FID(&config.cdw11),
            OPT_END(),
        };

        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_SEARCH_ENTRY_IDX.mode_str))
    {
        config.monitor_mode = MODE_SEARCH_ENTRY_IDX.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_IDX(&config.cdw11),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_FILE_ALLOC.mode_str))
    {
        io_cmd              = true;
        config.monitor_mode = MODE_FILE_ALLOC.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_FILE(&config.data_file),
            MONITOR_MODE_OPT_FTYPE(&config.nmc_new_mapping_filetype),
            MONITOR_MODE_OPT_CNT(&config.nmc_new_mapping_nblks),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_FILE_FREE.mode_str))
    {
        io_cmd              = true;
        config.monitor_mode = MODE_FILE_FREE.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_FORCE(&config.cdw15),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    // else if (!strcmp(mode, MODE_.mode_str))
    // {
    //     config.monitor_mode = MODE_.mode_code;
    //     OPT_ARGS(cmdOpts) = {
    //         MONITOR_MODE_OPT_DRY(&config.dry),
    //         OPT_END(),
    //     };
    //     VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    // }
    else
    {
        pr_info("Available modes:");
        for (size_t iMode = 0; iMode < (sizeof(MONITOR_MODES) / sizeof(monitor_mode_t)); ++iMode)
            pr_info("\t %s", MONITOR_MODES[iMode].mode_str);

        assert_return(0, -1, "Usage: ./nvme nmc subcommand DEV mode options");
    }

    // parse options and send request
    err = parse_and_open(&config.dev, config.argc, config.argv, "monitor", opts);
    assert_return(!err, err, "Failed to parse the options or open the target dev '%s'...", argv[1]);

    if (io_cmd)
    {
        config.NSID = OPENSSD_NSID;

        if (config.monitor_mode == MODE_FILE_ALLOC.mode_code)
        {
            config.OPCODE   = IO_NVM_NMC_ALLOC;
            config.data_len = NMC_FILENAME_MAX_BYTES;
            config.data     = aligned_alloc(getpagesize(), config.data_len);

            config.monitor_mode = 0;
            config.slba         = 0;
            config.nlb          = 0;

            // copy filename
            memset(config.data, 0, config.data_len);
            snprintf(config.data, config.data_len, "%s", config.data_file);
        }
        else if (config.monitor_mode == MODE_FILE_FREE.mode_code)
            config.OPCODE = IO_NVM_NMC_FLUSH;
    }

    err = nmc_send_passthru(io_cmd, config);

    if (config.monitor_mode == MODE_FILE_ALLOC.mode_code)
        free(config.data);

    return err;
}

static int monitor_buffer(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    // define subcommands
    const char *mode = (argv[1] && argv[2]) ? argv[2] : "(null)"; // subcmd dev mode opts

    const monitor_mode_t MODE_DUMP_DIRTY         = {"dump-dirty", 1};
    const monitor_mode_t MODE_DUMP_LBA           = {"dump-lba", 2};
    const monitor_mode_t MODE_DUMP_LBA_RANGE     = {"dump-lb-range", 3};
    const monitor_mode_t MODE_DUMP_SLICE_BUFFER  = {"dump-slice", 4};
    const monitor_mode_t MODE_CLEAR_SLICE_BUFFER = {"clear-slice", 5};

    const monitor_mode_t MONITOR_MODES[] = {
        MODE_DUMP_DIRTY,        MODE_DUMP_LBA,           MODE_DUMP_LBA_RANGE,
        MODE_DUMP_SLICE_BUFFER, MODE_CLEAR_SLICE_BUFFER,
    };

    // check subcommand and setup options
    int err;
    bool io_cmd = false;

    nmc_config_t config = {.argc = argc, .argv = argv, .OPCODE = ADMIN_MONITOR_BUFFER};
    OPT_ARGS(opts)      = {[0 ... 20] = OPT_END()};

    if (!strcmp(mode, MODE_DUMP_DIRTY.mode_str))
    {
        config.monitor_mode = MODE_DUMP_DIRTY.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_DUMP_LBA.mode_str))
    {
        config.monitor_mode = MODE_DUMP_LBA.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_LBA(&config.cdw11),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_DUMP_LBA_RANGE.mode_str))
    {
        config.monitor_mode = MODE_DUMP_LBA_RANGE.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_SLBA(&config.cdw11),
            MONITOR_MODE_OPT_ELBA(&config.cdw12),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_DUMP_SLICE_BUFFER.mode_str))
    {
        config.monitor_mode = MODE_DUMP_SLICE_BUFFER.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_IDIE(&config.cdw11),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_CLEAR_SLICE_BUFFER.mode_str))
    {
        config.monitor_mode = MODE_CLEAR_SLICE_BUFFER.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_IDIE(&config.cdw11),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    // else if (!strcmp(mode, MODE_.mode_str))
    // {
    //     config.monitor_mode = MODE_.mode_code;
    //     OPT_ARGS(cmdOpts) = {
    //         MONITOR_MODE_OPT_NSID(&config.NSID),
    //         MONITOR_MODE_OPT_DRY(&config.dry),
    //         OPT_END(),
    //     };
    //     VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    // }
    else
    {
        pr_info("Available modes:");
        for (size_t iMode = 0; iMode < (sizeof(MONITOR_MODES) / sizeof(monitor_mode_t)); ++iMode)
            pr_info("\t %s", MONITOR_MODES[iMode].mode_str);

        assert_return(0, -1, "Usage: ./nvme nmc subcommand DEV mode options");
    }

    // parse options and send request
    err = parse_and_open(&config.dev, config.argc, config.argv, "monitor", opts);
    assert_return(!err, err, "Failed to parse the options or open the target dev '%s'...", argv[1]);

    err = nmc_send_passthru(io_cmd, config);
    return err;
}

static int monitor_flash(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    // define subcommands
    const char *mode = (argv[1] && argv[2]) ? argv[2] : "(null)"; // subcmd dev mode opts

    const monitor_mode_t MODE_DUMP_FREE_BLKS = {"dump-free-blks", 1};
    const monitor_mode_t MODE_DUMP_PHY_PAGE  = {"dump-phy-page", 2};
    const monitor_mode_t MODE_READ_PHY_PAGE  = {"read-phy-page", 3};
    // const monitor_mode_t MODE_WRITE_PHY_PAGE  = {"write-phy-page", 3};
    const monitor_mode_t MODE_ERASE_PHY_BLK = {"erase-phy-blk", 4};

    const monitor_mode_t MONITOR_MODES[] = {
        MODE_DUMP_FREE_BLKS,
        MODE_DUMP_PHY_PAGE,
        MODE_READ_PHY_PAGE,
        // MODE_WRITE_PHY_PAGE,
        MODE_ERASE_PHY_BLK,
    };

    // check subcommand and setup options
    int err;
    bool io_cmd = false;

    nmc_config_t config = {.argc = argc, .argv = argv, .OPCODE = ADMIN_MONITOR_FLASH};
    OPT_ARGS(opts)      = {[0 ... 20] = OPT_END()};

    if (!strcmp(mode, MODE_DUMP_FREE_BLKS.mode_str))
    {
        config.monitor_mode = MODE_DUMP_FREE_BLKS.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_IDIE(&config.cdw11),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_DUMP_PHY_PAGE.mode_str))
    {
        config.monitor_mode = MODE_DUMP_PHY_PAGE.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_IDIE(&config.cdw11),
            MONITOR_MODE_OPT_IBLK(&config.cdw12),
            MONITOR_MODE_OPT_IPAGE(&config.cdw13),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_READ_PHY_PAGE.mode_str))
    {
        config.monitor_mode = MODE_READ_PHY_PAGE.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_FILE(&config.data_file),
            MONITOR_MODE_OPT_IDIE(&config.iDie),
            MONITOR_MODE_OPT_IBLK(&config.iBlk),
            MONITOR_MODE_OPT_IPAGE(&config.iPage), // force each opt 1 line
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    // else if (!strcmp(mode, MODE_WRITE_PHY_PAGE.mode_str))
    // {
    //     config.monitor_mode = MODE_WRITE_PHY_PAGE.mode_code;
    //     OPT_ARGS(cmdOpts) = {
    //         MONITOR_MODE_OPT_DRY(&config.dry),
    //         OPT_END(),
    //     };
    //     VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    // }
    else if (!strcmp(mode, MODE_ERASE_PHY_BLK.mode_str))
    {
        config.monitor_mode = MODE_ERASE_PHY_BLK.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_IDIE(&config.cdw11),
            MONITOR_MODE_OPT_IBLK(&config.cdw12),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    // else if (!strcmp(mode, MODE_.mode_str))
    // {
    //     config.monitor_mode = MODE_.mode_code;
    //     OPT_ARGS(cmdOpts) = {
    //         MONITOR_MODE_OPT_DRY(&config.dry),
    //         OPT_END(),
    //     };
    //     VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    // }
    else
    {
        pr_info("Available modes:");
        for (size_t iMode = 0; iMode < (sizeof(MONITOR_MODES) / sizeof(monitor_mode_t)); ++iMode)
            pr_info("\t %s", MONITOR_MODES[iMode].mode_str);
        assert_return(0, -1, "Usage: ./nvme nmc subcommand DEV mode options");
    }

    // parse options and send request
    err = parse_and_open(&config.dev, config.argc, config.argv, "monitor", opts);
    assert_return(!err, err, "Failed to parse the options or open the target dev '%s'...", argv[1]);

    // io-passthru DEV NSID -o=0x92 -r -l 16384 --cdw10=134217756 --cdw12=3 -b
    if (config.monitor_mode == MODE_READ_PHY_PAGE.mode_code)
    {
        config.iCh  = VDIE2PCH(config.iDie);
        config.iWay = VDIE2PWAY(config.iDie);

        // replace config
        config.NSID     = OPENSSD_NSID;
        config.OPCODE   = 0x92; // IO_NVM_READ_PHY
        config.data_len = BYTES_PER_PAGE;
        config.data     = aligned_alloc(getpagesize(), BYTES_PER_PAGE);

        pr_info("Target flash page info:");
        pr_info("\t iCh = %u", config.iCh);
        pr_info("\t iWay = %u", config.iWay);
        pr_info("\t iBlk = %u", config.iBlk);
        pr_info("\t iPage = %u", config.iPage);

        // convert die+row to vsa: "(iBlk << 14) + (iPage << 6) + (iWay << 3) + iCh"
        config.slba = (config.iBlk << 14) + (config.iPage << 6) + (config.iWay << 3) + config.iCh;
        // convert vsa to lba: "vsa << 2"
        config.slba *= 4;
        config.nlb = 3; // cdw12, 4 lba (each 4096, 0 based)

        io_cmd = true;
    }

    err = nmc_send_passthru(io_cmd, config);

    // NOTE: after nmc_send_passthru, config may be changed
    if (config.data)
    {
        if (!err && config.data_file)
            _flush_page_to_file_bin(config.iCh, config.data, config.data_file);
        free(config.data);
    }

    return err;
}

static int monitor_mapping(int argc, char **argv, struct command *cmd, struct plugin *plugin)
{
    // define subcommands
    const char *mode = (argv[1] && argv[2]) ? argv[2] : "(null)"; // subcmd dev mode opts

    const monitor_mode_t MODE_DUMP_LSA = {"dump-lsa", 1};
    const monitor_mode_t MODE_DUMP_VSA = {"dump-vsa", 2};
    const monitor_mode_t MODE_SET_L2V  = {"set-l2v", 3};

    const monitor_mode_t MONITOR_MODES[] = {
        MODE_DUMP_LSA,
        MODE_DUMP_VSA,
        MODE_SET_L2V,
    };

    // check subcommand and setup options
    int err;
    bool io_cmd = false;

    nmc_config_t config = {.argc = argc, .argv = argv, .OPCODE = ADMIN_MONITOR_FLASH};
    OPT_ARGS(opts)      = {[0 ... 20] = OPT_END()};

    if (!strcmp(mode, MODE_DUMP_LSA.mode_str))
    {
        config.monitor_mode = MODE_DUMP_LSA.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_LSA(&config.cdw11),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_DUMP_VSA.mode_str))
    {
        config.monitor_mode = MODE_DUMP_VSA.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_VSA(&config.cdw11),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    else if (!strcmp(mode, MODE_SET_L2V.mode_str))
    {
        config.monitor_mode = MODE_SET_L2V.mode_code;
        OPT_ARGS(cmdOpts)   = {
            MONITOR_MODE_OPT_DRY(&config.dry),
            MONITOR_MODE_OPT_LSA(&config.cdw11),
            MONITOR_MODE_OPT_VSA(&config.cdw12),
            OPT_END(),
        };
        VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    }
    // else if (!strcmp(mode, MODE_.mode_str))
    // {
    //     config.monitor_mode = MODE_.mode_code;
    //     OPT_ARGS(cmdOpts) = {
    //         MONITOR_MODE_OPT_DRY(&config.dry),
    //         OPT_END(),
    //     };
    //     VERIFY_SET_MONITOR_MODE_OPTS(opts, cmdOpts);
    // }
    else
    {
        pr_info("Available modes:");
        for (size_t iMode = 0; iMode < (sizeof(MONITOR_MODES) / sizeof(monitor_mode_t)); ++iMode)
            pr_info("\t %s", MONITOR_MODES[iMode].mode_str);
        assert_return(0, -1, "Usage: ./nvme nmc subcommand DEV mode options");
    }

    // parse options and send request
    err = parse_and_open(&config.dev, config.argc, config.argv, "monitor", opts);
    assert_return(!err, err, "Failed to parse the options or open the target dev '%s'...", argv[1]);

    err = nmc_send_passthru(io_cmd, config);
    return err;
}




static int monitor_print(int argc, char **argv, struct command *cmd, struct plugin *plugin){
    pr_info("moniter print start");
    int err;
    nmc_config_t config = {.argc = argc, .argv = argv, .NSID = OPENSSD_NSID};
    const char *mode = (argv[1] && argv[2]) ? argv[2] : "(null)"; // subcmd dev mode opts
    pr_info("argv[1]: %s",argv[1]);
    pr_info("argv[2]: %s",argv[2]);
    const monitor_mode_t prt_host_print = {"dump-free-blks", 1};
    // const monitor_mode_t MODE_DUMP_PHY_PAGE  = {"dump-phy-page", 2};
    // const monitor_mode_t MODE_READ_PHY_PAGE  = {"read-phy-page", 3};
}
