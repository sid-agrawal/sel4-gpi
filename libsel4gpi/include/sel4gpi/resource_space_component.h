#pragma once

#include <stdint.h>
#include <sel4/sel4.h>

#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/pd_obj.h>

/**
 * Eventually this will be a fully-fledged component. 
 * For now, it is just implemented by the pd_component.
*/

// (XXX) Arya: to be replaced with resource spaces
typedef struct _pd_component_resource_manager_entry
{
    resource_server_registry_node_t gen;

    gpi_cap_t resource_type;
    seL4_CPtr server_ep;
    pd_t *pd;
    uint64_t ns_index; // Tracks the last allocated namespace ID
} pd_component_resource_manager_entry_t;