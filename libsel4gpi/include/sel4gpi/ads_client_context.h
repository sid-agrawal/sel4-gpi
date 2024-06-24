/**
 * @file ads_client_context.h
 * @brief The ADS Client context - this is in a separate file to prevent circular dependencies
 */
#pragma once
#include <sel4/sel4.h>
#include <stdint.h>

typedef struct _ads_client_context
{
   cspacepath_t badged_server_ep_cspath;
   uint64_t id;
} ads_client_context_t;

typedef struct _ads_vmr_context
{
   cspacepath_t badged_server_ep_cspath;
} ads_vmr_context_t;

