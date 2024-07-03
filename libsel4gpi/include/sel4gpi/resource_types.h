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
    GPICAP_TYPE_ADS,    ///< An address space
    GPICAP_TYPE_VMR,    ///< A virtual memory region
    GPICAP_TYPE_MO,     ///< A memory object
    GPICAP_TYPE_CPU,    ///< A CPU object, currently primarily a TCB
    GPICAP_TYPE_PCPU,   ///< Physical CPU core
    GPICAP_TYPE_PD,     ///< A PD
    GPICAP_TYPE_EP,     ///< An endpoint, this is not an actual OSmosis model resource type
    GPICAP_TYPE_RESSPC, ///< A resource space
    GPICAP_TYPE_seL4,   ///< An seL4 kernel object
} gpi_cap_t;

#define GPICAP_TYPE_MAX (GPICAP_TYPE_seL4 + 8) // Maximum number of resource types

/**
 * Fields to uniquely identify a resource
 */
typedef struct _gpi_res_id
{
    gpi_cap_t type;
    uint32_t space_id;
    uint32_t object_id;
} gpi_res_id_t;

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

/**
 * Convenience function to generate a unique resource ID struct
 * 
 * @param type the resource type
 * @param space_id the resource space ID
 * @param object_id the resource object ID (unique within the space)
 */
gpi_res_id_t make_res_id(gpi_cap_t type, uint32_t space_id, uint32_t object_id);