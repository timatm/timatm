#include "model_parser.h"
#include <fcntl.h>
#include <unistd.h>
#include <regex.h> // for checking string postfix

// #define DEBUG

#include "../flash_config.h"
#include "../debug.h"

/* -------------------------------------------------------------------------- */
/*                            utility data handlers                           */
/* -------------------------------------------------------------------------- */

ONNX_PARSER_RET ONNX_DATA_HDR__CONV__GET_SHAPE(ONNX_PARSER_ARGS args);
ONNX_PARSER_RET ONNX_DATA_HDR__CONV__GET_KERNEL(ONNX_PARSER_ARGS args);
ONNX_PARSER_RET ONNX_DATA_HDR__CONV__GET_BIAS(ONNX_PARSER_ARGS args);
ONNX_PARSER_RET ONNX_DATA_HDR__MUL__GET_CONST(ONNX_PARSER_ARGS args);
ONNX_PARSER_RET ONNX_DATA_HDR__MUL__SKIP(ONNX_PARSER_ARGS args);
ONNX_PARSER_RET ONNX_DATA_HDR__MUL__GET_M0(ONNX_PARSER_ARGS args);
ONNX_PARSER_RET ONNX_DATA_HDR__NEG__GET_N(ONNX_PARSER_ARGS args);
ONNX_PARSER_RET ONNX_DATA_HDR__MAXPOOL__GET_SHAPE(ONNX_PARSER_ARGS args);
ONNX_PARSER_RET ONNX_DATA_HDR__FLUSH(ONNX_PARSER_ARGS args);

/* -------------------------------------------------------------------------- */
/*                      implementations for parsing model                     */
/* -------------------------------------------------------------------------- */

typedef struct
{
    bool updateInConvShape;
    bool updateInConvKernel;
    bool updateInConvBias;

    size_t idxPrevConv;
    size_t idxPrevMul;
    size_t idxPrevNeg;
} ONNX_NODE_COUNTER_t;

int parse_onnx_unet(const Onnx__ModelProto *model, const ONNX_LAYER_t *layers, size_t n_layers)
{
    void *private = &(ONNX_NODE_COUNTER_t){
        .idxPrevConv        = 0,
        .idxPrevMul         = 0,
        .idxPrevNeg         = 0,
        .updateInConvShape  = false,
        .updateInConvKernel = false,
        .updateInConvBias   = true,
    };

    const ONNX_LAYER_t UNET_CONV = {.name       = "Conv_[0-9]+",
                                    .n_handlers = 2,
                                    .handlers   = (ONNX_DATA_HANDLER[]){
                                        ONNX_DATA_HDR__CONV__GET_KERNEL,
                                        ONNX_DATA_HDR__CONV__GET_BIAS,
                                    }};

    const ONNX_LAYER_t UNET_CONV_TP = {.name       = "ConvTranspose_[0-9]+",
                                       .n_handlers = 2,
                                       .handlers   = (ONNX_DATA_HANDLER[]){
                                           ONNX_DATA_HDR__CONV__GET_KERNEL,
                                           ONNX_DATA_HDR__CONV__GET_BIAS,
                                       }};

    const ONNX_LAYER_t UNET_MUL = {.name       = "Mul_[0-9]+",
                                   .n_handlers = 1,
                                   .handlers   = (ONNX_DATA_HANDLER[]){
                                       ONNX_DATA_HDR__MUL__GET_M0,
                                   }};

    const ONNX_LAYER_t UNET_SKIP_MUL = {.name       = "Mul_[0-9]+",
                                        .n_handlers = 1,
                                        .handlers   = (ONNX_DATA_HANDLER[]){
                                            ONNX_DATA_HDR__MUL__SKIP,
                                        }};

    const ONNX_LAYER_t UNET_FLUSH = {.name       = NULL,
                                     .n_handlers = 1,
                                     .handlers   = (ONNX_DATA_HANDLER[]){
                                         ONNX_DATA_HDR__FLUSH,
                                     }};

    const ONNX_LAYER_t UNET_NEG = {.name       = "Neg_[0-9]+",
                                   .n_handlers = 1,
                                   .handlers   = (ONNX_DATA_HANDLER[]){ONNX_DATA_HDR__NEG__GET_N}};

    ONNX_LAYER_t GROUP_CMN[]       = {UNET_CONV, UNET_MUL, UNET_NEG};
    ONNX_LAYER_t GROUP_2CMNTP2MN[] = {UNET_CONV,    UNET_MUL, UNET_NEG, //
                                      UNET_CONV,    UNET_MUL, UNET_NEG, //
                                      UNET_CONV_TP, UNET_MUL, UNET_NEG, UNET_MUL, UNET_NEG};

    ONNX_LAYER_GROUP_t unet[] = {
        {.n_repeats = 1, SET_ONNX_LAYERS((ONNX_LAYER_t[]){UNET_SKIP_MUL})},
        {.n_repeats = 8, SET_ONNX_LAYERS(GROUP_CMN)},
        {.n_repeats = 4, SET_ONNX_LAYERS(GROUP_2CMNTP2MN)},
        {.n_repeats = 4, SET_ONNX_LAYERS(GROUP_CMN)},
        {.n_repeats = 1, SET_ONNX_LAYERS((ONNX_LAYER_t[]){UNET_FLUSH})},
    };

    // parse model info
    return parse_onnx(model, unet, NUM_ONNX_LAYER_GRPS(unet), private);
}

typedef enum
{
    ONNX_DTYPE__UINT32_TO_UINT8,
    ONNX_DTYPE__FLOAT,
    ONNX_DTYPE__FLOAT_TO_INT8,
    ONNX_DTYPE__FLOAT_TO_UINT8,
    ONNX_DTYPE__FLOAT_TO_INT16,

    ONNX_DTYPE_ADMIN__SET_CHANNEL,
    ONNX_DTYPE_ADMIN__FLUSH,
    ONNX_DTYPE_ADMIN__DUMP,
} ONNX_DTYPE_t;

void generic_data_hdr(void *addr, ONNX_DTYPE_t dtype, size_t idx)
{
    union
    {
        uint8_t bytes[8]; // 64 bits at most
        uint8_t int8;
        uint8_t uint8;
        uint8_t int16;
        uint8_t uint16;
        uint64_t uint64;
        float float32;
    } *src = addr, dst = {.uint64 = 0};

    /*
     * check data type
     */

    switch (dtype)
    {
    case ONNX_DTYPE__UINT32_TO_UINT8: /* layer shape */
        dst.uint8 = (uint8_t)src->uint64;
        pr("\tint32[%lu] = %u", idx, dst.uint8);
        break;

    case ONNX_DTYPE__FLOAT_TO_INT8: /* conv weights range = int8 */
        dst.int8 = (int8_t)src->float32;
        pr("\tfloat[%lu] -> %d (int8)", idx, dst.int8);
        break;

    case ONNX_DTYPE__FLOAT_TO_INT16: /* bias range = int16 */
        dst.int16 = (int16_t)src->float32;
        pr("\tfloat[%lu] -> %d (int16)", idx, dst.int16);
        break;

    case ONNX_DTYPE__FLOAT_TO_UINT8: /* requantize factos range = uint8 */
        dst.int8 = (uint8_t)src->float32;
        pr("\tfloat[%lu] -> %d (uint8)", idx, dst.int8);
        break;

    case ONNX_DTYPE_ADMIN__FLUSH: /* flush remaining data */
        pr("flush all remaining data");
        flush_all_model_data();
        return;

    default:
        pr_error("Unsupported data type: %u...", dtype);
        return;
    }

    // check data size
    size_t bytes = 0;

    switch (dtype)
    {
    case ONNX_DTYPE__UINT32_TO_UINT8:
    case ONNX_DTYPE__FLOAT_TO_UINT8:
    case ONNX_DTYPE__FLOAT_TO_INT8: /* 1 Byte */
        bytes = 1;
        break;
    case ONNX_DTYPE__FLOAT_TO_INT16: /* 2 Bytes */
        bytes = 2;
        break;
    default:
        pr_info("Unexpected ONNX_DTYPE: %u", dtype);
        break;
    }

    // send data to user buffer
    for (size_t iByte = 0; iByte < bytes; ++iByte)
        append_model_data(dst.bytes[iByte], iByte == (bytes - 1));

#if 0 // dump weights to single file
    FILE *f = fopen("logs/weights.bin", "a");
    for (size_t iByte = 0; iByte < bytes; ++iByte)
        assert(fwrite(&dst.bytes[iByte], 1, 1, f) == 1);
    fclose(f);
#endif
}

/* -------------------------------------------------------------------------- */
/*                             node data handlers                             */
/* -------------------------------------------------------------------------- */

#define REGEX_FMT_CONV_KERNEL ".*\\.weight$"
#define REGEX_FMT_CONV_BIAS   ".*\\.bias$"

ONNX_PARSER_RET ONNX_DATA_HDR__CONV__GET_SHAPE(ONNX_PARSER_ARGS args)
{
    ONNX_NODE_COUNTER_t *counter = (ONNX_NODE_COUNTER_t *)args.private_data;

    int iNode = search_onnx_node_first(args.graph, args.node_name, counter->idxPrevConv + 1);
    if (iNode < 0)
    {
        pr_error("Cannot find the node named '%s'", args.node_name);
        return ONNX_PARSER_RET_FAILED;
    }
    if (counter->updateInConvShape)
        counter->idxPrevConv = iNode;

    Onnx__NodeProto *node = args.graph->node[iNode];
    for (size_t iAttr = 0; iAttr < node->n_attribute; ++iAttr)
    {
        if (!strcmp(node->attribute[iAttr]->name, "kernel_shape"))
        {
            pr_info("Conv Kernel Shape for '%s':", node->name);
            for (size_t iDim = 0; iDim < node->attribute[iAttr]->n_ints; ++iDim)
                generic_data_hdr(&node->attribute[iAttr]->ints[iDim], ONNX_DTYPE__UINT32_TO_UINT8,
                                 iDim);

            return ONNX_PARSER_RET_DONE;
        }
    }

    return ONNX_PARSER_RET_FAILED;
}

ONNX_PARSER_RET ONNX_DATA_HDR__CONV__GET_KERNEL(ONNX_PARSER_ARGS args)
{
    regex_t pat;
    regmatch_t res[1];

    if (regcomp(&pat, REGEX_FMT_CONV_KERNEL, REG_EXTENDED))
    {
        pr_error("Failed to compile regex ...");
        return ONNX_PARSER_RET_FAILED;
    }

    ONNX_NODE_COUNTER_t *counter = (ONNX_NODE_COUNTER_t *)args.private_data;

    int iNode = search_onnx_node_first(args.graph, args.node_name, counter->idxPrevConv + 1);
    if (iNode < 0)
    {
        pr_error("Cannot find the node named '%s'", args.node_name);
        return ONNX_PARSER_RET_FAILED;
    }
    if (counter->updateInConvKernel)
        counter->idxPrevConv = iNode;

    // check all input nodes
    Onnx__NodeProto *node   = args.graph->node[iNode];
    Onnx__TensorProto *init = NULL;

    for (size_t iInput = 0; iInput < node->n_input; ++iInput)
    {
        if (!regexec(&pat, node->input[iInput], 1, res, 0))
        {
            pr_debug("Input '%s' is kernel weights", node->input[iInput]);
            init = search_onnx_initializer(args.graph, node->input[iInput]);
        }
    }

    if (init)
    {
        // FIXME: support more data type
        assert(init->data_type == ONNX__TENSOR_PROTO__DATA_TYPE__FLOAT);

        // total number of weights
        size_t n_weights = 1;
        pr_info("Conv Shape for '%s':", node->name);
        for (size_t iDim = 0; iDim < init->n_dims; n_weights *= init->dims[iDim], ++iDim)
            generic_data_hdr(&init->dims[iDim], ONNX_DTYPE__UINT32_TO_UINT8, iDim);

        // sequentially access the weights
        pr_info("Conv Weights for '%s':", node->name);
        for (size_t iWeight = 0; iWeight < n_weights; ++iWeight)
            generic_data_hdr(&(((float *)init->raw_data.data)[iWeight]), ONNX_DTYPE__FLOAT_TO_INT8,
                             iWeight);

        return ONNX_PARSER_RET_DONE;
    }

    return ONNX_PARSER_RET_FAILED;
}

ONNX_PARSER_RET ONNX_DATA_HDR__CONV__GET_BIAS(ONNX_PARSER_ARGS args)
{
    regex_t pat;
    regmatch_t res[1];

    if (regcomp(&pat, REGEX_FMT_CONV_BIAS, REG_EXTENDED))
    {
        pr_error("Failed to compile regex ...");
        return ONNX_PARSER_RET_FAILED;
    }

    ONNX_NODE_COUNTER_t *counter = (ONNX_NODE_COUNTER_t *)args.private_data;

    int iNode = search_onnx_node_first(args.graph, args.node_name, counter->idxPrevConv + 1);
    if (iNode < 0)
    {
        pr_error("Cannot find the node named '%s'", args.node_name);
        return ONNX_PARSER_RET_FAILED;
    }
    if (counter->updateInConvBias)
        counter->idxPrevConv = iNode;

    // check all input nodes
    Onnx__NodeProto *node   = args.graph->node[iNode];
    Onnx__TensorProto *init = NULL;

    for (size_t iInput = 0; iInput < node->n_input; ++iInput)
    {
        if (!regexec(&pat, node->input[iInput], 1, res, 0))
        {
            pr_debug("Input '%s' is bias", node->input[iInput]);
            init = search_onnx_initializer(args.graph, node->input[iInput]);
        }
    }

    if (init)
    {
        // FIXME: support more data type
        assert(init->n_dims == 1 && init->data_type == ONNX__TENSOR_PROTO__DATA_TYPE__FLOAT);

        pr_info("Conv Bias for '%s':", node->name);
        for (size_t iBias = 0; iBias < init->dims[0]; ++iBias)
            generic_data_hdr(&((float *)init->raw_data.data)[iBias], ONNX_DTYPE__FLOAT_TO_INT16,
                             iBias);

        return ONNX_PARSER_RET_DONE;
    }

    return ONNX_PARSER_RET_FAILED;
}

ONNX_PARSER_RET ONNX_DATA_HDR__MUL__GET_CONST(ONNX_PARSER_ARGS args)
{
    Onnx__NodeProto *node  = search_onnx_node(args.graph, args.node_name);
    Onnx__NodeProto *cnode = NULL;
    Onnx__TensorProto *value;

    if (!node)
    {
        pr_error("Cannot find the node named '%s'", args.node_name);
        return ONNX_PARSER_RET_FAILED;
    }

    for (size_t iInput = 0; iInput < node->n_input; ++iInput)
    {
        cnode = search_onnx_constant_by_output(args.graph, node->input[iInput]);
        if (!cnode)
            continue;

        // target constant node found, check attributes
        for (size_t iAttr = 0; iAttr < cnode->n_attribute; ++iAttr)
        {
            // FIXME: ensure type and attr
            value = cnode->attribute[iAttr]->t;

            pr_info("Mul Constants for '%s':", args.node_name);
            generic_data_hdr((float *)value->raw_data.data, ONNX_DTYPE__FLOAT,
                             0); // FIXME (m/(2^e))

            return ONNX_PARSER_RET_DONE;
        }
    }

    return ONNX_PARSER_RET_FAILED;
}

ONNX_PARSER_RET ONNX_DATA_HDR__MUL__SKIP(ONNX_PARSER_ARGS args)
{
    ONNX_NODE_COUNTER_t *counter = (ONNX_NODE_COUNTER_t *)args.private_data;

    int iNode = search_onnx_node_first(args.graph, args.node_name, counter->idxPrevMul + 1);
    if (iNode >= 0)
    {
        counter->idxPrevMul = iNode;
        return ONNX_PARSER_RET_DONE;
    }
    pr_error("Cannot find the node named '%s'", args.node_name);
    return ONNX_PARSER_RET_FAILED;
}

ONNX_PARSER_RET ONNX_DATA_HDR__MUL__GET_M0(ONNX_PARSER_ARGS args)
{
    ONNX_NODE_COUNTER_t *counter = (ONNX_NODE_COUNTER_t *)args.private_data;

    Onnx__NodeProto *mul = NULL, *cnode = NULL;
    Onnx__TensorProto *value;

    int iNode;
    do
    {
        iNode = search_onnx_node_first(args.graph, args.node_name, counter->idxPrevMul + 1);
        if (iNode < 0)
        {
            pr_error("Cannot find the node named '%s'", args.node_name);
            return ONNX_PARSER_RET_FAILED;
        }

        counter->idxPrevMul = iNode;

        // the target mul node's output should not be Round and Clip
        mul = args.graph->node[iNode];
        if (strncmp(mul->output[0], "onnx::Round", strlen("onnx::Round")) &&
            strncmp(mul->output[0], "onnx::Clip", strlen("onnx::Clip")))
            break;

    } while (iNode >= 0);

    // find const node whose output is mul's input[1]
    for (size_t iInput = 0; iInput < mul->n_input; ++iInput)
    {
        cnode = search_onnx_constant_by_output(args.graph, mul->input[iInput]);
        if (!cnode)
            continue;

        // target constant node found, check attributes
        for (size_t iAttr = 0; iAttr < cnode->n_attribute; ++iAttr)
        {
            value = cnode->attribute[iAttr]->t; // FIXME: ensure type and attr

            pr_info("Requantized factor 'M0': %u", (uint8_t)(*(float *)value->raw_data.data));
            generic_data_hdr(value->raw_data.data, ONNX_DTYPE__FLOAT_TO_UINT8, 0);

            return ONNX_PARSER_RET_DONE;
        }
    }

    pr_error("Cannot find the node for requantize factor m0 in %s", mul->name);
    return ONNX_PARSER_RET_FAILED;
}

ONNX_PARSER_RET ONNX_DATA_HDR__NEG__GET_N(ONNX_PARSER_ARGS args)
{
    ONNX_NODE_COUNTER_t *counter = (ONNX_NODE_COUNTER_t *)args.private_data;

    Onnx__NodeProto *neg = NULL, *cnode = NULL;
    Onnx__TensorProto *init;

    int iNode = search_onnx_node_first(args.graph, args.node_name, counter->idxPrevNeg + 1);
    if (iNode < 0)
    {
        pr_error("Cannot find the node named '%s'", args.node_name);
        return ONNX_PARSER_RET_FAILED;
    }
    else
        counter->idxPrevNeg = iNode;

    // find init node for requantize_scale_e
    neg = args.graph->node[iNode];

    assert(neg->n_input == 1);
    init = search_onnx_initializer(args.graph, neg->input[0]);

    if (!init) // fallback to identity node's input
    {
        cnode = search_onnx_node_by_type_output(args.graph, "Identity", neg->input[0]);
        assert(cnode->n_input == 1);
        init = search_onnx_initializer(args.graph, cnode->input[0]);
    }

    if (init)
    {
        pr_info("Requantized factor 'n': %u", (uint8_t)(*(float *)init->raw_data.data));
        generic_data_hdr(init->raw_data.data, ONNX_DTYPE__FLOAT_TO_UINT8, 0);

        return ONNX_PARSER_RET_DONE;
    }

    pr_error("Cannot find the node for requantize factor n in %s", neg->name);
    return ONNX_PARSER_RET_FAILED;
}

ONNX_PARSER_RET ONNX_DATA_HDR__MAXPOOL__GET_SHAPE(ONNX_PARSER_ARGS args)
{
    Onnx__NodeProto *node = search_onnx_node(args.graph, args.node_name);

    if (!node)
    {
        pr_error("Cannot find the node named '%s'", args.node_name);
        return ONNX_PARSER_RET_FAILED;
    }

    for (size_t iAttr = 0; iAttr < node->n_attribute; ++iAttr)
    {
        if (!strcmp(node->attribute[iAttr]->name, "kernel_shape"))
        {
            pr_info("MaxPool Shape for '%s':", args.node_name);
            for (size_t iDim = 0; iDim < node->attribute[iAttr]->n_ints; ++iDim)
                generic_data_hdr(&node->attribute[iAttr]->ints[iDim], ONNX_DTYPE__UINT32_TO_UINT8,
                                 iDim);

            return ONNX_PARSER_RET_DONE;
        }
    }

    return ONNX_PARSER_RET_FAILED;
}

ONNX_PARSER_RET ONNX_DATA_HDR__FLUSH(ONNX_PARSER_ARGS args)
{
    generic_data_hdr(NULL, ONNX_DTYPE_ADMIN__FLUSH, 0);
    return ONNX_PARSER_RET_DONE;
}
