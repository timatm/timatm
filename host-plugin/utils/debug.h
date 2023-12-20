#ifndef __NMC_HOST_PLUGIN_DEBUG_H__
#define __NMC_HOST_PLUGIN_DEBUG_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

/* -------------------------------------------------------------------------- */
/*                              ANSI escape code                              */
/* -------------------------------------------------------------------------- */

#define PR_RESET //"\e[0m"
#define PR_DEBUG //"\e[32;49;1mDEBUG "
#define PR_INFO  //"\e[34;49;1mINFO "
#define PR_ERROR //"\e[31;49;1mERROR "

/* -------------------------------------------------------------------------- */
/*                               logging macros                               */
/* -------------------------------------------------------------------------- */

#define CODE_POS_FMT  "(%s() at %s:%d):: "
#define CODE_POS_ARGS __func__, __FILE__, __LINE__

#define INDENT_UNIT        4
#define pr(fmt, ...)       printf(fmt "\r\n", ##__VA_ARGS__)
#define pr_lv(n, fmt, ...) pr("%*s" fmt, ((n)*INDENT_UNIT), "", ##__VA_ARGS__)

#ifdef DEBUG
#define pr_debug(fmt, ...) pr(PR_DEBUG CODE_POS_FMT PR_RESET fmt, CODE_POS_ARGS, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...)
#endif

#define pr_info(fmt, ...)  pr(PR_INFO PR_RESET fmt, ##__VA_ARGS__)
#define pr_error(fmt, ...) pr(PR_ERROR CODE_POS_FMT PR_RESET fmt, CODE_POS_ARGS, ##__VA_ARGS__)

#define assert_return(cond, ret, fmt, ...)                                                                   \
    ({                                                                                                       \
        if (!(cond))                                                                                         \
        {                                                                                                    \
            pr_error("assert failed: " fmt, ##__VA_ARGS__);                                                  \
            return (ret);                                                                                    \
        }                                                                                                    \
    })

#define assert_goto(cond, label, fmt, ...)                                                                   \
    ({                                                                                                       \
        if (!(cond))                                                                                         \
        {                                                                                                    \
            pr_error("assert failed: " fmt, ##__VA_ARGS__);                                                  \
            goto label;                                                                                      \
        }                                                                                                    \
    })

#define assert_exit(cond, fmt, ...)                                                                          \
    ({                                                                                                       \
        if (!(cond))                                                                                         \
        {                                                                                                    \
            pr_error("assert failed: " fmt, ##__VA_ARGS__);                                                  \
            exit(-1);                                                                                        \
        };                                                                                                   \
    })

#endif /* __NMC_HOST_PLUGIN_DEBUG_H__ */