#ifndef __NMC_HOST_PLUGIN_ONNX_MODEL_PARSER_H__
#define __NMC_HOST_PLUGIN_ONNX_MODEL_PARSER_H__

#include <stdbool.h>
#include "onnx.proto3.pb-c.h"

/* -------------------------------------------------------------------------- */
/*                       data structures for ONNX parser                      */
/* -------------------------------------------------------------------------- */

typedef enum
{
    ONNX_PARSER_RET_DONE   = 0,
    ONNX_PARSER_RET_FAILED = -1,
} ONNX_PARSER_RET;

typedef struct
{
    Onnx__GraphProto *graph;
    char *node_name;
    void *private_data;
} ONNX_PARSER_ARGS;

typedef ONNX_PARSER_RET (*ONNX_DATA_HANDLER)(ONNX_PARSER_ARGS);

typedef struct
{
    char *name;
    size_t n_handlers;
    const ONNX_DATA_HANDLER *handlers;
} ONNX_LAYER_t;

typedef struct
{
    size_t n_repeats;
    size_t n_layers;
    const ONNX_LAYER_t *layers;
} ONNX_LAYER_GROUP_t;

#define NUM_ONNX_LAYERS(layerArr)   (sizeof(layerArr) / sizeof(ONNX_LAYER_t))
#define NUM_ONNX_LAYER_GRPS(grpArr) (sizeof(grpArr) / sizeof(ONNX_LAYER_GROUP_t))
#define SET_ONNX_LAYERS(layerArr)   .n_layers = NUM_ONNX_LAYERS(layerArr), .layers = layerArr

/* -------------------------------------------------------------------------- */
/*                               main interfaces                              */
/* -------------------------------------------------------------------------- */

Onnx__ModelProto *try_unpack_onnx(const char *path);
int parse_onnx(const Onnx__ModelProto *m, const ONNX_LAYER_GROUP_t *grps, size_t n, void *private);

#if (USER_MODEL_DATA_APPENDER == true)
#pragma message "Use user defined logic for appending MODEL data"
extern void append_model_data(uint8_t data, bool end);
#else
#pragma message "Use default logic for appending MODEL data (no buffer)"
void append_model_data(uint8_t data, bool end) { pr_debug("Dummy data appender"); }
#endif

#if (USER_MODEL_DATA_FLUSH_ALL == true)
#pragma message "Use user defined logic for flushing all MODEL data"
extern void flush_all_model_data();
#else
#pragma message "Use default logic for flushing all MODEL data (no flush)"
void flush_all_model_data() {}
#endif

/* -------------------------------------------------------------------------- */
/*                              utility functions                             */
/* -------------------------------------------------------------------------- */

Onnx__NodeProto *search_onnx_node(const Onnx__GraphProto *graph, const char *name);
int search_onnx_node_first(const Onnx__GraphProto *g, const char *fmt, size_t off);
Onnx__NodeProto *search_onnx_node_by_type_output(const Onnx__GraphProto *g, const char *type,
                                                 const char *name);
Onnx__NodeProto *search_onnx_constant_by_output(const Onnx__GraphProto *graph, const char *name);
Onnx__TensorProto *search_onnx_initializer(const Onnx__GraphProto *graph, const char *name);

#endif /* __NMC_HOST_PLUGIN_ONNX_MODEL_PARSER_H__ */