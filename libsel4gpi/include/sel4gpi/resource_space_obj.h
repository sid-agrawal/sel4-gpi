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
    pd_t *pd; // (XXX) Arya: this should probably be a pd ID
    void *data; // Generic field for additional resource space data
} res_space_t;