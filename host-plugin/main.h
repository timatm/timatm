/* SPDX-License-Identifier: GPL-2.0-or-later */
// clang-format off
#undef CMD_INC_FILE
#define CMD_INC_FILE plugins/nmc/main

#if !defined(NVME_CLI_NMC_PLUGIN) || defined(CMD_HEADER_MULTI_READ)
#define NVME_CLI_NMC_PLUGIN

#include "cmd.h"

PLUGIN(NAME("nmc", "Near Memory Computing Command Set", NVME_VERSION),
       COMMAND_LIST(
		ENTRY("inference", "Inference the specified TIFF image.", inference)
		ENTRY("write-model", "Write an onnx model with the predefined placement strategy.", write_model)
		ENTRY("write-tiff", "Write a TIFF image with a predefined placement policy. (w/ libtiff)", write_tiff)

		ENTRY("inference-read", "", inference_read)
		ENTRY("monitor-nmc-mapping", "", monitor_nmc_mapping)
		ENTRY("monitor-buffer", "", monitor_buffer)
		ENTRY("monitor-flash", "", monitor_flash)
		ENTRY("monitor-mapping", "", monitor_mapping)
		ENTRY("monitor-print", "", monitor_print)
		
	)
);

#endif

#include "define_cmd.h"
// clang-format on