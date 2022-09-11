
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#define OSMOSIS_DEBUG 0
#if OSMOSIS_DEBUG
#define OSDB_PRINTF(...) printf(__VA_ARGS__)
#else
#define OSDB_PRINTF(...) 
#endif