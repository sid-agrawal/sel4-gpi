#pragma once

/**
 * General utility functions for error handling in GPI functions
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

#define GOTO_IF_ERR(err, msg, ...)      \
    do                                  \
    {                                   \
        if ((err))                      \
        {                               \
            printf(msg, ##__VA_ARGS__); \
            goto err_goto;              \
        }                               \
    } while (0)

#define GOTO_IF_COND(c, msg, ...)       \
    do                                  \
    {                                   \
        if ((c))                        \
        {                               \
            printf(msg, ##__VA_ARGS__); \
            goto err_goto;              \
        }                               \
    } while (0)

// (XXX) Arya: Occasionally I see a weird issue
// where depending on the number of VA_ARGS, there
// will be a page fault if the inner printf gets compiled
#define SERVER_GOTO_IF_ERR(err, msg, ...)                       \
    do                                                          \
    {                                                           \
        if ((err))                                              \
        {                                                       \
            OSDB_PRINTERR("%s: " msg, __func__, ##__VA_ARGS__); \
            goto err_goto;                                      \
        }                                                       \
    } while (0)

#define SERVER_GOTO_IF_COND(c, msg, ...)                        \
    do                                                          \
    {                                                           \
        if ((c))                                                \
        {                                                       \
            OSDB_PRINTERR("%s: " msg, __func__, ##__VA_ARGS__); \
            error = 1;                                          \
            goto err_goto;                                      \
        }                                                       \
    } while (0)

/* also prints the given badge in human-readable format */
#define SERVER_GOTO_IF_COND_BG(c, badge, msg, ...)              \
    do                                                          \
    {                                                           \
        if ((c))                                                \
        {                                                       \
            OSDB_PRINTERR("%s: " msg, __func__, ##__VA_ARGS__); \
            badge_print((badge));                               \
            error = 1;                                          \
            goto err_goto;                                      \
        }                                                       \
    } while (0)

#define SERVER_PRINT_IF_ERR(err, msg, ...)                      \
    do                                                          \
    {                                                           \
        if ((err))                                              \
        {                                                       \
            OSDB_PRINTERR("%s: " msg, __func__, ##__VA_ARGS__); \
        }                                                       \
    } while (0)
