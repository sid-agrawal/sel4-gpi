#pragma once

#include <stdint.h>
#include <sel4/sel4.h>

#define RESOURCE_TYPE_MAX_STRING_SIZE 16

/**
 * Resource type is either:
 * - Core type: one of the enum values defined for GPICAP_CORE_TYPE
 * - Dynamic type: dynamic value assigned by alloc_new_resource_type
*/
typedef enum GPICAP_CORE_TYPE
{
    // Core cap types
    GPICAP_TYPE_NONE = 0,
    GPICAP_TYPE_ADS,
    GPICAP_TYPE_VMR,
    GPICAP_TYPE_MO,
    GPICAP_TYPE_CPU,
    GPICAP_TYPE_PCPU,
    GPICAP_TYPE_PD,
    GPICAP_TYPE_RESSPC, // resource space
    GPICAP_TYPE_seL4,

    // Non-core cap types
    GPICAP_TYPE_BLOCK,
    GPICAP_TYPE_FILE,

    GPICAP_TYPE_MAX,
} gpi_cap_t;

/**
 * Gets the gpi_cap_t code for a resource type by name
 * If the resource type does not yet exist, allocates a new code
 * 
 * @param name A text name for the resource type
 * @return the new resource type code
*/
gpi_cap_t get_resource_type_code(char *name);

/**
 * Get a text name for a cap code
 * 
 * @param cap_type the cap code
 * @return the text name of the cap code
*/
char *cap_type_to_str(gpi_cap_t cap_type);

/**
 * Initialize the GPI server's registry of resource types and populate with core types
*/
void resource_types_initialize(void);