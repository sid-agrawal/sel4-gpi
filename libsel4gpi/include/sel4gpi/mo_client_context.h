/**
 * @file mo_client_context.h
 * @brief The MO Client context - this is in a separate file to prevent circular dependencies
 */
#pragma once
#include <sel4/sel4.h>
#include <stdint.h>

typedef struct _mo_client_context
{
   cspacepath_t badged_server_ep_cspath;
   // cspacepath_t public_server_ep_cspath;
   uint64_t id; // Needed only for RR dump
} mo_client_context_t;
