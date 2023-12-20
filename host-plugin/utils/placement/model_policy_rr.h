#ifndef __NMC_HOST_PLUGIN_MODEL_PLACEMENT_RR_H__
#define __NMC_HOST_PLUGIN_MODEL_PLACEMENT_RR_H__

#include <stdint.h>
#include <stddef.h>

#include "../flash_config.h"

/* -------------------------------------------------------------------------- */
/*                               public members                               */
/* -------------------------------------------------------------------------- */

void model_data_force_flush_all();
void model_data_appender_rr(uint8_t data, bool isEnd);

#endif /* __NMC_HOST_PLUGIN_MODEL_PLACEMENT_RR_H__ */