#pragma once
#include <utils/ansi_color.h>

/**
 * General utility functions for error handling in GPI functions.
 * Whether or not these are displayed are controlled by
 * OSBG_LEVEL and OSDB_TOPIC from \ref debug.h
 *
 * Usage:
 *
 * GOTO_IF_ERR / GOTO_IF_COND
 * - Specify a err_goto line within the function
 *
 * SERVER_GOTO_IF_COND
 * - Use within a resource server, to return an error tag
 * - The DEBUG_ID and SERVER_ID values must be defined
 */

#define UNCONDITIONAL_PRINTERR(msg, ...) \
    OSDB_LVL_PRINT(OSDB_ERROR, 1, COLORIZE("[ERROR] %s():\t", RED) msg, __func__, ##__VA_ARGS__)

#define UNCONDITIONAL_WARN(msg, ...) \
    OSDB_LVL_PRINT(OSDB_WARN, 1, COLORIZE("[WARNING] %s():\t", YELLOW) msg, __func__, ##__VA_ARGS__);

#define GOTO_IF_ERR(err, msg, ...)                      \
    do                                                  \
    {                                                   \
        if ((err))                                      \
        {                                               \
            UNCONDITIONAL_PRINTERR(msg, ##__VA_ARGS__); \
            goto err_goto;                              \
        }                                               \
    } while (0)

#define GOTO_IF_COND(c, msg, ...)                       \
    do                                                  \
    {                                                   \
        if ((c))                                        \
        {                                               \
            error = 1;                                  \
            UNCONDITIONAL_PRINTERR(msg, ##__VA_ARGS__); \
            goto err_goto;                              \
        }                                               \
    } while (0)

#define FATAL_IF_ERR(err, msg, ...)                               \
    do                                                            \
    {                                                             \
        if ((err))                                                \
        {                                                         \
            printf(COLORIZE("[FATAL] ", RED) msg, ##__VA_ARGS__); \
            abort();                                              \
        }                                                         \
    } while (0)

#define WARN_IF_COND(c, msg, ...)                   \
    do                                              \
    {                                               \
        if ((c))                                    \
        {                                           \
            UNCONDITIONAL_WARN(msg, ##__VA_ARGS__); \
        }                                           \
    } while (0)

/* ===================== Server functions =====================*/

// (XXX) Arya: Occasionally I see a weird issue
// where depending on the number of VA_ARGS, there
// will be a page fault if the inner printf gets compiled
#define SERVER_GOTO_IF_ERR(err, msg, ...)                          \
    do                                                             \
    {                                                              \
        if ((err))                                                 \
        {                                                          \
            OSDB_PRINTERR("%s():\t" msg, __func__, ##__VA_ARGS__); \
            goto err_goto;                                         \
        }                                                          \
    } while (0)

#define SERVER_GOTO_IF_COND(c, msg, ...)                           \
    do                                                             \
    {                                                              \
        if ((c))                                                   \
        {                                                          \
            OSDB_PRINTERR("%s():\t" msg, __func__, ##__VA_ARGS__); \
            error = 1;                                             \
            goto err_goto;                                         \
        }                                                          \
    } while (0)

/* also prints the given badge in human-readable format */
#define SERVER_GOTO_IF_COND_BG(c, badge, msg, ...)                 \
    do                                                             \
    {                                                              \
        if ((c))                                                   \
        {                                                          \
            OSDB_PRINTERR("%s():\t" msg, __func__, ##__VA_ARGS__); \
            BADGE_PRINT((badge));                                  \
            error = 1;                                             \
            goto err_goto;                                         \
        }                                                          \
    } while (0)

#define SERVER_WARN_IF_COND(c, msg, ...)                            \
    do                                                              \
    {                                                               \
        if ((c))                                                    \
        {                                                           \
            OSDB_PRINTWARN("%s():\t" msg, __func__, ##__VA_ARGS__); \
        }                                                           \
    } while (0)

/* also prints the given badge in human-readable format */
#define SERVER_WARN_IF_COND_BG(c, badge, msg, ...)                  \
    do                                                              \
    {                                                               \
        if ((c))                                                    \
        {                                                           \
            OSDB_PRINTWARN("%s():\t" msg, __func__, ##__VA_ARGS__); \
            BADGE_PRINT((badge));                                   \
        }                                                           \
    } while (0)
