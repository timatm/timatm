#include "common.h"
#include "model_policy_rr.h"

#include <stdlib.h>

#include "../debug.h"

// #define USER_FLUSH_MODEL true

/* -------------------------------------------------------------------------- */
/*                              internal members                              */
/* -------------------------------------------------------------------------- */

static size_t FC_BUFFER_OFFS[NUM_CHANNELS] = {0}; // channel offset for next data
static uint8_t FC_BUFFERS[NUM_FLASH_CHANNELS][BYTES_PER_PAGE];

static uint8_t iCurrentFC  = 0;                      // channel number for next data
static size_t bytesFCQuota = BYTES_PER_CYCLE_PER_FC; // for RR-like policy

#if (USER_FLUSH_MODEL == true)
#pragma message "Use user defined logic for MODEL flush"
extern void flush_page_model(uint8_t iFC, uint8_t *data);
#else
#pragma message "Use default logic for MODEL flush (flush as text file)"
void flush_page_model(uint8_t iFC, uint8_t *data)
{
    pr_debug("FC[%u]: flush model buffer", iFC);
    _flush_page_to_file(iFC, data, "logs/model-buffer");
}
#endif /* USER_FLUSH_MODEL */

/* -------------------------------------------------------------------------- */
/*                              public utilities                              */
/* -------------------------------------------------------------------------- */

void model_data_force_flush_all()
{
    // flush all buffers
    for (uint8_t iFC = 0; iFC < NUM_FLASH_CHANNELS; ++iFC)
    {
        // padding zeros before flushing
        memset(&FC_BUFFERS[iFC][FC_BUFFER_OFFS[iFC]], 0, BYTES_PER_PAGE - FC_BUFFER_OFFS[iFC]);
        flush_page_model(iFC, FC_BUFFERS[iFC]);

        // reset cursor
        FC_BUFFER_OFFS[iFC] = 0;
    }

    // reset global status
    iCurrentFC   = 0;
    bytesFCQuota = BYTES_PER_CYCLE_PER_FC;
}

void model_data_appender_rr(uint8_t data, bool isEnd)
{
    pr_debug("FC[%u].Byte[%lu]: append '%d'", iCurrentFC, FC_BUFFER_OFFS[iCurrentFC], data);

    // copy data to FC buffer and update buffer offset
    FC_BUFFERS[iCurrentFC][FC_BUFFER_OFFS[iCurrentFC]] = data;
    FC_BUFFER_OFFS[iCurrentFC]++;

    // check buffer size and flush
    if (FC_BUFFER_OFFS[iCurrentFC] == BYTES_PER_PAGE)
    {
        flush_page_model(iCurrentFC, FC_BUFFERS[iCurrentFC]);
        FC_BUFFER_OFFS[iCurrentFC] = 0;
    }

    // move to next FC if no quota
    bytesFCQuota -= 1;
    if (!bytesFCQuota)
    {
        iCurrentFC   = (iCurrentFC + 1) % NUM_CHANNELS;
        bytesFCQuota = BYTES_PER_CYCLE_PER_FC;
    }
}
