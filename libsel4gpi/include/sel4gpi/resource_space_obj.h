#pragma once

#include <stdint.h>
#include <sel4/sel4.h>

#include <sel4gpi/badge_usage.h>

// (XXX) Arya: Workaround for circular dependency, to be fixed
typedef struct _pd pd_t;

typedef struct _res_space
{
    uint32_t id;
    gpi_cap_t resource_type;
    seL4_CPtr server_ep;
    pd_t *pd;
    uint64_t ns_index; // Tracks the last allocated namespace ID
} res_space_t;