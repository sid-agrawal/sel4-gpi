
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#define PD_DEBUG 1
#define CPU_DEBUG 2
#define ADS_DEBUG 4
#define MO_DEBUG 8
#define GPI_DEBUG 16
#define ALL_DEBUG (PD_DEBUG | CPU_DEBUG | ADS_DEBUG | MO_DEBUG | GPI_DEBUG)
#define OSMOSIS_DEBUG ALL_DEBUG // selectively enable component debug e.g. (PD_DEBUG | ADS_DEBUG)

#if OSMOSIS_DEBUG
#define OSDB_PRINTF(component, ...)                     \
    do                                                  \
    {                                                   \
        if ((OSMOSIS_DEBUG & (component)) == component) \
        {                                               \
            printf(__VA_ARGS__);                        \
        }                                               \
    } while (0)
#else
#define OSDB_PRINTF(...)
#endif
