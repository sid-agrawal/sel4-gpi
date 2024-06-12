
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <sel4utils/process.h>
#include <sel4utils/process_config.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>
#include <utils/uthash.h>

#include <sel4gpi/badge_usage.h>
#include <sel4gpi/cap_tracking.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/cpu_obj.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/linked_list.h>

#define TEST_NAME_MAX (64 - 4 * sizeof(seL4_Word))
#define MAX_SYS_OSM_CAPS 5000
#define MAX_MO_CHILD 10

#define MAX_PD_NAME 64
#define MAX_PD_OSM_CAPS 512
#define MAX_PD_OSM_RDE GPICAP_TYPE_MAX *MAX_NS_PER_RDE
#define MAX_NS_PER_RDE 8

#define MAX_PD_INIT_CAPS 8

#define PD_ALLOCATOR_STATIC_POOL_SIZE ((1 << seL4_PageBits) * 20)

// (XXX) This is not yet used anywher.
typedef struct pd_name
{
    char top[MAX_PD_NAME];
    char mid[MAX_PD_NAME];
    char end[MAX_PD_NAME];
} pd_name_t;

typedef union rde_type
{
    // We have talked about tracking RDE for speicific resdources,
    // For instance, say one FILE cap came from one PD, and another from another.
    // Or if a resources was handed down from another PD, should that PD be the RDE,
    // or the PD that that created it should be RDE.
    // seL4_Word slot_in_PD;
    gpi_cap_t type;
} rde_type_t;

// Tracks a request edge from a PD
typedef struct osmosis_rde
{
    // The slot of the RDE cap as per seL4
    seL4_Word slot_in_RT;
    seL4_Word slot_in_PD;

    /*
        I think that type+id should be all we need
        to find out when OSM resources are shared.
        But let's keep track of slot_in* (above) for now.
    */

    /* RDE is for a particular resource space */
    rde_type_t type; // (XXX) Arya: This may be redundant given the space id
    uint32_t space_id;
} osmosis_rde_t;

// Tracks a resource that a PD holds
typedef struct _pd_hold_node
{
    // UTHash key is a badge constructed from type, space_id, and res_id
    resource_server_registry_node_t gen;

    // The slot of the cap as per seL4
    // do not rely on these, as this info is sometimes difficult to find
    seL4_Word slot_in_PD_Debug;
    seL4_Word slot_in_RT_Debug;
    seL4_Word slot_in_ServerPD_Debug; // For instance in case of file.

    /*
        I think that type+space_id+res_id should be all we need
        to find out when OSM resources are shared.
        But let's keep track of slot_in* (above) for now.
    */
    gpi_cap_t type;
    uint32_t space_id;
    uint32_t res_id;

} pd_hold_node_t;

/**
 * The data given to initialize a new Osmosis PD
 */
typedef struct _osm_pd_shared_data
{
    pd_client_context_t pd_conn;   ///< Connection to the PD's own PD resource
    ads_client_context_t ads_conn; ///< Connection to the PD's own ADS resource
    cpu_client_context_t cpu_conn; ///< Connection to the PD's own CPU resource

    seL4_CPtr cspace_root; ///< PD's cspace

    char type_names[GPICAP_TYPE_MAX][RESOURCE_TYPE_MAX_STRING_SIZE]; ///< Friendly names of cap types
    osmosis_rde_t rde[GPICAP_TYPE_MAX][MAX_NS_PER_RDE];              ///< Resource directory
    uint64_t rde_count;

    uint64_t current_client_id; ///< Resource server sets this field while processing a client request
                                ///< If the server crashes before it finishes, the client will also be killed
    seL4_CPtr reply_cap;        ///< For resource servers, store the reply cap of the
                                ///< request that is currently being processed
} osm_pd_shared_data_t;

typedef struct _pd
{
    uint32_t id;

    /* Fields only used for process PDs */
    sel4utils_process_t proc;
    uintptr_t sysinfo;
    int num_elf_phdrs;
    Elf64_Phdr *elf_phdrs;
    seL4_Word pagesz;

    /* Fields for all PDs */
    const char *image_name;                                 ///< This is for model extraction only
    seL4_Word cnode_guard;                                  ///< cnode guard for this PD's cspace
    vka_t *pd_vka;                                          ///< Allocator for the PD's cspace
    char allocator_mem_pool[PD_ALLOCATOR_STATIC_POOL_SIZE]; ///< Memory pool to bootstrap the PD's VKA
    resource_server_registry_t hold_registry;               ///< Registry of PD's resources

    uint64_t shared_data_mo_id;              ///< Shared data is mapped to PD and includes RDE, etc.
    osm_pd_shared_data_t *shared_data;       ///< RT vaddr of the shared data
    osm_pd_shared_data_t *shared_data_in_PD; ///< PD's vaddr of the shared

    /* other general PD metadata */
    bool deleted; ///< Set to true while the PD is being deleted
} pd_t;

/**
 * @brief Creates a new PD Object
 *
 * @param pd allocated, empty PD struct
 * @param server_vka vka object to allocate frames from
 * @param server_vspace unused
 * @param osm_data_mo an MO to hold the PD's OSmosis data
 * @return int
 */
int pd_new(pd_t *pd,
           vka_t *server_vka,
           vspace_t *server_vspace,
           mo_t *osm_data_mo);

int pd_dump(pd_t *pd);

/**
 * Send a cap to another PD's CSpace (badge and copy), and also adds it to the PD's resources set (if applicable)
 *
 * @param pd the target PD
 * @param cap the cap to send
 * @param the badge of the cap to send
 * @param slot returns the slot of the cap in the target PD
 * @param inc_refcount if true, increments the refcount of the corresponding resource
 * @param update_core_res the cap being sent is a core PD resource,
 *                        and should be set in its OSmosis data
 */
int pd_send_cap(pd_t *pd,
                seL4_CPtr cap,
                seL4_Word badge,
                seL4_Word *slot,
                bool inc_refcount,
                bool update_core_res);

/**
 * @brief Allocate a free slot from the PD's cspace
 * 
 * @param pd the target PD
 * @param next_free_slot returns the allocated slot index
 * @return 0 on success, error otherwise
*/
int pd_next_slot(pd_t *pd,
                 seL4_CPtr *next_free_slot);

/**
 * @brief Free a slot from the PD's cspace
 * 
 * @param pd the target PD
 * @param slot slot index to free
 * @return 0 on success, error otherwise
*/
int pd_free_slot(pd_t *pd,
                 seL4_CPtr slot);

/**
 * @brief Delete the capability from a slot in the PD's cspace
 * 
 * @param pd the target PD
 * @param slot slot index to clear
 * @return 0 on success, error otherwise
*/
int pd_clear_slot(pd_t *pd,
                 seL4_CPtr slot);

/**
 * Allocates an endpoint using the gpi server's vka, and copies to the pd cspace
 *
 * @param pd The pd to allocate an endpoint for
 * @param server_vka VKA of the gpi server
 * @param ret_ep slot of the allocated ep in the PD's cspace
 */
int pd_alloc_ep(pd_t *pd,
                vka_t *server_vka,
                seL4_CPtr *ret_ep);

/**
 * Bootstraps a VKA allocator for the PD's cspace
 * Requires an existing 1-level cspace
 *
 * @param root The root cnode for a 1-level cspace
 * @param start_slot First free slot in the cspace
 * @param end_slot Last free slot in the cspace
 * @param size_bits Size bits of the entire cspace
 * @param guard_bits Number of guard bits used for this cspace
 */
int pd_bootstrap_allocator(pd_t *pd,
                           seL4_CPtr root,
                           size_t start_slot,
                           size_t end_slot,
                           size_t size_bits,
                           size_t guard_bits);

void print_pd_osm_cap_info(pd_hold_node_t *o);
void print_pd_osm_rde_info(osmosis_rde_t *o);

/**
 * Add a resource that the PD holds in metadata only, the resource isn't actually minted into the PD's cspace
 * Does not insert if the resource ID is a duplicate
 *
 * @param type the resource type
 * @param space_id the resource space ID
 * @param res_id the resource ID (unique within the space)
 * @param slot_in_RT for debugging purposes, may be removed
 * @param slot_in_PD for debugging purposes, may be removed
 * @param slot_in_serverPD for debugging purposes, may be removed
 * @return 0 on success, 1 otherwise
 */
int pd_add_resource(pd_t *pd,
                    gpi_cap_t type,
                    uint32_t space_id,
                    uint32_t res_id,
                    seL4_CPtr slot_in_RT,
                    seL4_CPtr slot_in_PD,
                    seL4_CPtr slot_in_serverPD);

/**
 * Remove a resource that the PD holds, also revoke / delete the cap in the PD's cspace
 * Does nothing if the PD does not hold the resource
 *
 * @param type the resource type
 * @param space_id the resource space ID
 * @param res_id the resource ID (unique within the space)
 * @return 0 on success, 1 otherwise
 */
int pd_remove_resource(pd_t *pd,
                       gpi_cap_t type,
                       uint32_t space_id,
                       uint32_t res_id);
/**
 * @brief Adds all resources specified in the given list to a PD
 *
 * @param pd the target PD
 * @param resources resources to add to the PD
 * @return int 0 on success, an indicated error may still have successfully added some resources
 */
int pd_bulk_add_resource(pd_t *pd, linked_list_t *resources);

/**
 * Remove all resources in the given space ID from the PD
 *
 * @param pd the target PD
 * @param space_id remove all resources in this space from the PD
 * @return 0 on success, error otherwise
 */
int pd_remove_resources_in_space(pd_t *pd, uint32_t space_id);

/**
 * @brief gets all resources of the given type that belongs to the given PD
 *
 * @param pd the PD to find resources from
 * @param type the type of resource to find
 * @return linked_list_t* a list of all the found resources
 */
linked_list_t *pd_get_resources_of_type(pd_t *pd, gpi_cap_t type);

/**
 * Add a resource type name so a PD may access it by cap_type_to_str
 *
 * @param pd The target PD
 * @param type the type
 * @param type_name friendly name of the type
 */
void pd_add_type_name(pd_t *pd,
                      rde_type_t type,
                      char *type_name);

/**
 * @brief Add an RDE to a PD
 *
 * @param pd The target PD to add an RDE to
 * @param type the type of the RDE
 * @param type_name friendly name of the type, used to get RDE entries by name
 * @param space_id the resource space of this RDE
 * @param server_ep the raw endpoint of the resource space
 */
int pd_add_rde(pd_t *pd,
               rde_type_t type,
               char *type_name,
               uint32_t space_id,
               seL4_CPtr server_ep);

/**
 * @brief Remove an RDE from a PD
 *
 * @param pd The target PD to remove an RDE from
 * @param type the type of the RDE
 * @param space_id the resource space of this RDE
 * @return 0 on success, error otherwise
 */
int pd_remove_rde(pd_t *pd,
                  rde_type_t type,
                  uint32_t space_id);

/**
 * @brief Send a cap to a PD's cspace without badging (does NOT add the cap to the PD's resources set)
 *
 * @param to_pd destination PD
 * @param cap the cap to send
 * @param slot the slot in destination cspace
 */
int copy_cap_to_pd(pd_t *to_pd,
                   seL4_CPtr cap,
                   seL4_Word *slot);

/**
 * Gets the entry of the PD's RDE corresponding
 * to the type and space id
 * (XXX) Arya: Maybe poor design that we need this at all
 *
 * @param pd the target PD
 * @param type The RDE type
 * @param space_id The space id, or RESSPC_ID_NULL for the default
 * @return The seL4_CPtr of the RDE in the current cspace,
 *         or seL4_CapNull if not found
 */
osmosis_rde_t *pd_rde_get(pd_t *pd,
                          gpi_cap_t type,
                          uint32_t space_id);

/**
 * Destroys a PD object
 * Destroys all resources internal to the PD
 * Also triggers deletion of any resources if this was the last PD holding them
 *
 * This does not remove the PD from the PD component registry
 * This function should only be called by the PD component
 *
 * @param pd the cpu object
 * @param server_vka
 * @param server_vspace
 */
void pd_destroy(pd_t *pd, vka_t *server_vka, vspace_t *server_vspace);

/**
 * @brief sets the PD's (process) image name for model exporting purposes
 *
 * @param pd the target PD
 * @param image_name name of ELF image
 */
void pd_set_image_name(pd_t *pd, const char *image_name);

/**
 * @brief debug print of all the PD's resources
 *
 * @param pd target PD
 */
void pd_debug_print_held(pd_t *pd);

/**
 * @brief sets the core caps in a PD's OSmosis data
 *
 * @param pd target PD
 * @param core_cap_badge badge of the resource
 * @param core_cap cap to the resource
 * @return int 0 on success
 */
int pd_set_core_cap(pd_t *pd, seL4_Word core_cap_badge, seL4_CPtr core_cap);

/**
 * Make a cspacepath for a slot in the PD's cspace
 *
 * @param pd target PD
 * @param cap a slot within the PD's cspace
 * @param path returns the full path to the given slot
 */
void pd_make_path(pd_t *pd, seL4_CPtr cap, cspacepath_t *path);