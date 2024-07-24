#pragma once

#include <stdint.h>
#include <sel4/sel4.h>

#include <sel4gpi/linked_list.h>
#include <sel4gpi/badge_usage.h>

typedef struct _res_space
{
    gpi_space_id_t id;        ///< Unique ID of the space
    gpi_cap_t resource_type;  ///< Type of resources in the space
    seL4_CPtr server_ep;      ///< Raw endpoint of the server that manages the resource space
    linked_list_t map_spaces; ///< List of res_space_t pointers, resource spaces that this space maps to
    gpi_obj_id_t pd_id;       ///< ID of the managing PD
    void *data;               ///< Generic field for additional resource space data (not used)
    bool to_delete;           ///< Marks a space that will be deleted in the next sweep
    bool cleanup_policy;      ///< Marks a space that will execute a cleanup policy in the next sweep
    bool deleting;            ///< Marks a space that is in the process of being deleted
    int deletion_depth;       ///< Mark the depth of this resource space for cleanup policy
} res_space_t;