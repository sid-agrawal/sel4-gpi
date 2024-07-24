/**
 * @file mo_client_context.h
 * @brief The MO Client context - this is in a separate file to prevent circular dependencies
 */
#pragma once
#include <sel4/sel4.h>
#include <stdint.h>

#define MO_PAGE_BITS seL4_PageBits
#define MO_LARGE_PAGE_BITS seL4_LargePageBits

typedef struct _mo_client_context
{
   seL4_CPtr ep;
   gpi_obj_id_t id; // Needed only for RR dump
} mo_client_context_t;
