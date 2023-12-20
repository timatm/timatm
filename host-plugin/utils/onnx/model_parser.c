#include "model_parser.h"

// #define DEBUG

#include "../debug.h"

#include <fcntl.h>
#include <unistd.h>
#include <regex.h> // for checking string postfix

/* -------------------------------------------------------------------------- */
/*                              utility functions                             */
/* -------------------------------------------------------------------------- */

Onnx__NodeProto *search_onnx_node(const Onnx__GraphProto *graph, const char *name)
{
    Onnx__NodeProto *target = NULL;

    pr_debug("Searching node by name '%s'...", name);
    for (size_t iNode = 0; iNode < graph->n_node; ++iNode)
    {
        if (!strcmp(graph->node[iNode]->name, name))
        {
            pr_debug("Node found at [%lu]!", iNode);
            target = graph->node[iNode];
            break;
        }
    }

    return target;
}

int search_onnx_node_first(const Onnx__GraphProto *g, const char *fmt, size_t off)
{
    regex_t pat;
    regmatch_t res[1];

    if (regcomp(&pat, fmt, REG_EXTENDED))
    {
        pr_error("Failed to compile regex format '%s'...", fmt);
        return ONNX_PARSER_RET_FAILED;
    }

    pr_debug("Searching first node matches the format: '%s' after node[%lu]...", fmt, off);
    for (size_t iNode = off; iNode < g->n_node; ++iNode)
    {
        if (!regexec(&pat, g->node[iNode]->name, 1, res, 0))
        {
            pr_debug("Node[%lu].name (%s) matches!", iNode, g->node[iNode]->name);
            return iNode;
        }
    }

    return -1;
}

Onnx__NodeProto *search_onnx_node_by_type_output(const Onnx__GraphProto *g, const char *type,
                                                 const char *name)
{
    Onnx__NodeProto *target = NULL;

    pr_debug("Searching '%s' node by output name '%s'...", type, name);
    for (size_t iNode = 0; iNode < g->n_node; ++iNode)
    {
        if ((g->node[iNode]->n_output == 1) && (!strcmp(g->node[iNode]->op_type, type)))
        {
            if (!strcmp(g->node[iNode]->output[0], name))
            {
                pr_debug("Node found at [%lu]!", iNode);
                target = g->node[iNode];
                break;
            }
        }
    }

    return target;
}

Onnx__NodeProto *search_onnx_constant_by_output(const Onnx__GraphProto *graph, const char *name)
{
    Onnx__NodeProto *target = NULL;

    pr_debug("Searching constant node by output name '%s'...", name);
    for (size_t iNode = 0; iNode < graph->n_node; ++iNode)
    {
        if ((graph->node[iNode]->n_output == 1) &&
            (!strcmp(graph->node[iNode]->op_type, "Constant")))
        {
            if (!strcmp(graph->node[iNode]->output[0], name))
            {
                pr_debug("Node found at [%lu]!", iNode);
                target = graph->node[iNode];
                break;
            }
        }
    }

    return target;
}

Onnx__TensorProto *search_onnx_initializer(const Onnx__GraphProto *graph, const char *name)
{
    Onnx__TensorProto *target = NULL;

    pr_debug("Searching initializer by name '%s'...", name);
    for (size_t iData = 0; iData < graph->n_initializer; ++iData)
    {
        if (!strcmp(graph->initializer[iData]->name, name))
        {
            pr_debug("Initializer found at [%lu]!", iData);
            target = graph->initializer[iData];
            break;
        }
    }

    return target;
}

/* -------------------------------------------------------------------------- */
/*                   public functions for parsing onnx model                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Try to unpack the specified onnx model file.
 *
 * @param path The path of target onnx model file.
 * @return Onnx__ModelProto* The unpacked model, or NULL if failed to unpack the specified model.
 */
Onnx__ModelProto *try_unpack_onnx(const char *path)
{
    FILE *f;
    uint8_t *buf;
    size_t bytes_model;
    Onnx__ModelProto *model;

    // check input model file
    pr_info("Try to parse the onnx model file: \"%s\"", path);

    if ((f = fopen(path, "rb")) == NULL)
    {
        pr_error("Cannot open the file '%s'...", path);
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0)
    {
        pr_error("Cannot read the file '%s'...", path);
        fclose(0);
        return NULL;
    }

    // get file size for buffer allocation
    bytes_model = ftell(f);
    rewind(f);
    pr_info("This onnx model's size is %lu bytes!", bytes_model);

    // read model file into buffer
    size_t ret;
    buf = malloc(bytes_model);
    if ((ret = fread(buf, 1, bytes_model, f)) != bytes_model)
    {
        pr_error("Expect to read %lu bytes, but get %lu only", bytes_model, ret);
        free(buf);
        fclose(f);
        return NULL;
    }

    // parse
    model = onnx__model_proto__unpack(NULL, bytes_model, buf);

    free(buf);
    fclose(f);
    return model;
}

int parse_onnx(const Onnx__ModelProto *m, const ONNX_LAYER_GROUP_t *grps, size_t n, void *private)
{
    ONNX_PARSER_ARGS args = {.graph = m->graph, .private_data = private};
    ONNX_PARSER_RET ret;

    // parse group by group
    for (size_t iGrp = 0; iGrp < n; ++iGrp)
    {
        ONNX_LAYER_GROUP_t grp = grps[iGrp];

        // parse layer by layer, and each layer may repeat several times
        for (size_t iRepeat = 0; iRepeat < grp.n_repeats; ++iRepeat)
            for (size_t iLayer = 0; iLayer < grp.n_layers; ++iLayer)
            {
                // call handlers
                for (size_t iHdr = 0; iHdr < grp.layers[iLayer].n_handlers; ++iHdr)
                {
                    args.node_name = grp.layers[iLayer].name;

                    if (grp.layers[iLayer].handlers[iHdr] == NULL)
                    {
                        pr_error("Handler[%lu] of layer[%lu] not exists, skipped...", iHdr, iLayer);
                        continue;
                    }

                    if (ONNX_PARSER_RET_DONE != (ret = grp.layers[iLayer].handlers[iHdr](args)))
                    {
                        pr_error("Callback for handler[%lu] of layer[%lu] return error", iHdr,
                                 iLayer);
                        return -1;
                    }
                }
            }
    }

    return 0;
}