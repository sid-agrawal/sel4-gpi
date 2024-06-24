/**
 * @file resource_space_client_context.h
 * @brief The Resource Space Client context - this is in a separate file to prevent circular dependencies
 */
#pragma once
#include <sel4/sel4.h>
#include <stdint.h>

typedef struct _resspc_client_context
{
   cspacepath_t badged_server_ep_cspath;
   uint64_t id;             ///< Resource space ID
   gpi_cap_t resource_type; ///< The type of resource that this resource space contains
} resspc_client_context_t;
