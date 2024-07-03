
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/resource_registry.h>
#include <sel4gpi/resource_types.h>
#include <sel4gpi/pd_utils.h>

static resource_registry_t *registry;

// Registry entry of a resource type
typedef struct _resource_type_registry_entry
{
    resource_registry_node_t gen;
    char name[RESOURCE_TYPE_MAX_STRING_SIZE];
} resource_type_registry_entry_t;

// Insert a resource type with an existing ID
static void insert_resource_type(gpi_cap_t id, char *name)
{
    resource_type_registry_entry_t *entry = malloc(sizeof(resource_type_registry_entry_t));
    entry->gen.object_id = id;
    strncpy(entry->name, name, RESOURCE_TYPE_MAX_STRING_SIZE);

    resource_registry_insert(registry, (resource_registry_node_t *)entry);
}

void resource_types_initialize(void)
{
    registry = &get_gpi_server()->resource_types;

    resource_registry_initialize(registry, NULL, NULL);

    // Insert the core cap types to the registry with their names
    insert_resource_type(GPICAP_TYPE_NONE, "NONE");
    insert_resource_type(GPICAP_TYPE_ADS, "ADS");
    insert_resource_type(GPICAP_TYPE_VMR, "VMR");
    insert_resource_type(GPICAP_TYPE_MO, "MO");
    insert_resource_type(GPICAP_TYPE_CPU, "VCPU");
    insert_resource_type(GPICAP_TYPE_PCPU, "PCPU");
    insert_resource_type(GPICAP_TYPE_PD, "PD");
    insert_resource_type(GPICAP_TYPE_RESSPC, "RESSPC");
    insert_resource_type(GPICAP_TYPE_EP, "EP");
    insert_resource_type(GPICAP_TYPE_seL4, "UNKNOWN");
}

// Insert a resource type and allocate a new ID
static gpi_cap_t alloc_new_resource_type(char *name)
{
    resource_type_registry_entry_t *entry = malloc(sizeof(resource_type_registry_entry_t));
    strncpy(entry->name, name, RESOURCE_TYPE_MAX_STRING_SIZE);

    resource_registry_insert_new_id(registry, (resource_registry_node_t *)entry);

    return entry->gen.object_id;
}

gpi_cap_t get_resource_type_code(char *name)
{
    // Check if the resource type already exists
    for (resource_type_registry_entry_t *curr = registry->head; curr != NULL; curr = curr->gen.hh.next)
    {
        if (strcmp(name, curr->name) == 0)
        {
            return (gpi_cap_t)curr->gen.object_id;
        }
    }

    // Othwerise, allocate a new resource type
    return alloc_new_resource_type(name);
}

char *cap_type_to_str(gpi_cap_t cap_type)
{
    if (get_gpi_server()->is_root)
    {
        if (registry == NULL)
        {
            // Can't get cap type name if the resource type registry is not initialized
            gpi_panic("Resource type registry is not initialized", 1);
        }

        // Root task finds name from resource type definitions
        resource_type_registry_entry_t *reg_entry = (resource_type_registry_entry_t *)
            resource_registry_get_by_id(registry, (uint64_t)cap_type);

        if (reg_entry == NULL)
        {
            gpi_panic("Cap type not found in registry: ", cap_type);
        }
        return reg_entry->name;
    } else {
        // Other PDs find the name from their init data
        return sel4gpi_get_resource_type_name(cap_type);
    }
}