/**
 * @file cpu_client_context.h
 * @brief The CPU Client context - this is in a separate file to prevent circular dependencies
 */
#pragma once
#include <sel4/sel4.h>
#include <stdint.h>

typedef struct _cpu_client_context
{
   seL4_CPtr ep;
   gpi_obj_id_t id;
} cpu_client_context_t;
