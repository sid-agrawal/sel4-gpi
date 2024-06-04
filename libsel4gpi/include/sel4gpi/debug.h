
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#define NO_DEBUG 0x0
#define PD_DEBUG 0x1
#define CPU_DEBUG 0x2
#define ADS_DEBUG 0x4
#define MO_DEBUG 0x8
#define RESSPC_DEBUG 0x10
#define GPI_DEBUG 0x20
#define FS_DEBUG 0x40 // (XXX) Arya: WIP to move remote resource server debug controls to this
#define ALL_DEBUG 0x7f
#define OSMOSIS_DEBUG NO_DEBUG                           // selectively enable component debug e.g. (PD_DEBUG | ADS_DEBUG)
#define OSMOSIS_ERROR ALL_DEBUG                          // selectively enable component error messages e.g. (PD_DEBUG | ADS_DEBUG)

#if OSMOSIS_DEBUG
// Utility print, requires DEBUG_ID and SERVER_ID defined
#define OSDB_PRINTF(...)                   \
    do                                     \
    {                                      \
        if (OSMOSIS_DEBUG & (DEBUG_ID))    \
        {                                  \
            printf(SERVER_ID __VA_ARGS__); \
        }                                  \
    } while (0)

// same as above, but prints without pre-pending the server ID
#define OSDB_PRINTF_2(...)              \
    do                                  \
    {                                   \
        if (OSMOSIS_DEBUG & (DEBUG_ID)) \
        {                               \
            printf(__VA_ARGS__);        \
        }                               \
    } while (0)
#else
#define OSDB_PRINTF(...)
#define OSDB_PRINTF_2(...)
#endif

#if OSMOSIS_ERROR
#define OSDB_PRINTERR(...)                 \
    do                                     \
    {                                      \
        if (OSMOSIS_ERROR & (DEBUG_ID))    \
        {                                  \
            printf(SERVER_ID __VA_ARGS__); \
        }                                  \
    } while (0)
#else
#define OSDB_PRINTERR(...)
#endif

/* For highlighting a certain print so that it's easier to see during debugging - should not remain in committed code */
#define CPRINTF(msg, ...)                              \
    do                                                 \
    {                                                  \
        printf(COLORIZE(msg, MAGENTA), ##__VA_ARGS__); \
    } while (0)
