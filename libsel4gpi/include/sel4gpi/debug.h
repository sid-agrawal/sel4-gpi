
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
#define ALL_DEBUG 0x3f
#define OSMOSIS_DEBUG CPU_DEBUG // selectively enable component debug e.g. (PD_DEBUG | ADS_DEBUG)
#define OSMOSIS_ERROR ALL_DEBUG // selectively enable component error messages e.g. (PD_DEBUG | ADS_DEBUG)

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

#define OSDB_PRINTF_2(component, ...)                   \
    do                                                  \
    {                                                   \
        if ((OSMOSIS_DEBUG & (component)) == component) \
        {                                               \
            printf(__VA_ARGS__);                        \
        }                                               \
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
