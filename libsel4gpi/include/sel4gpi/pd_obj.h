
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
typedef struct _osm_pd_init_data
{
    // Connection to the PD's own core resources
    pd_client_context_t pd_conn;
    ads_client_context_t ads_conn;
    cpu_client_context_t cpu_conn;

    // PD's cspace
    seL4_CPtr cspace_root;

    // Resource directory
    osmosis_rde_t rde[GPICAP_TYPE_MAX][MAX_NS_PER_RDE];
    uint64_t rde_count;
} osm_pd_init_data_t;

typedef struct _pd
{
    uint32_t id;

    /* This is only for model extraction purposes */
    const char *image_name;

    /* this is only used if the PD is a process */
    sel4utils_process_t proc;

    /* cnode guard for this PD's cspace */
    seL4_Word cnode_guard;

    /* allocator for the pd's cspace */
    char allocator_mem_pool[PD_ALLOCATOR_STATIC_POOL_SIZE];
    vka_t pd_vka;

    // PD's accessible resources
    resource_server_registry_t hold_registry;

    // Init data is mapped to PD and includes RDE and ADS/PD caps
    mo_client_context_t init_data_mo;
    seL4_CPtr init_data_frame;
    uint64_t init_data_mo_id;
    osm_pd_init_data_t *init_data;       // RT vaddr of the init data
    osm_pd_init_data_t *init_data_in_PD; // PD's vaddr of the init data

    seL4_CPtr pd_cap_in_RT;
} pd_t;

int pd_new(pd_t *pd,
           vka_t *server_vka,
           vspace_t *server_vspace,
           void *arg0);

int pd_configure(pd_t *pd,
                 ads_t *target_ads,
                 cpu_t *target_cpu);

int pd_dump(pd_t *pd);

/**
 * Send a cap to another PD
 *
 * @param pd the target PD
 * @param cap the cap to send
 * @param the badge of the cap to send
 * @param slot returns the slot of the cap in the target PD
 * @param inc_refcount if true, increments the refcount of the corresponding resource
 */
int pd_send_cap(pd_t *pd,
                seL4_CPtr cap,
                seL4_Word badge,
                seL4_Word *slot,
                bool inc_refcount);

int pd_start(pd_t *pd,
             vka_t *vka,
             seL4_CPtr pd_endpoint_in_root,
             vspace_t *vspace,
             int argc,
             seL4_Word *args);

int pd_next_slot(pd_t *pd,
                 seL4_CPtr *next_free_slot);

int pd_free_slot(pd_t *pd,
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
 * Mints a source path into the PD's cspace
 *
 * @param pd The destination PD
 * @param src Path to the source cap
 * @param badge Badge to apply
 * @param ret Returns the destination slot in the PD
 */
int pd_mint(pd_t *pd,
            cspacepath_t *src,
            seL4_Word badge,
            seL4_CPtr *ret);

/**
 * Mints an endpoint in the PD's cspace and attaches the badge
 *
 * @param pd The pd to allocate an endpoint for
 * @param src_ep raw endpoint to badge
 * @param badge badge to apply
 * @param ret_ep slot of the badged ep in the PD's cspace
 */
int pd_badge_ep(pd_t *pd,
                seL4_CPtr src_ep,
                seL4_Word badge,
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
 * Add a resource that the PD holds
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
 * @brief Add an RDE to a PD
 *
 * Note: This must be called after the PD is loaded, and before it is started
 *
 * @param pd The target PD to add an RDE to
 * @param type the type of the RDE
 * @param space_id the resource space of this RDE
 * @param server_ep the raw endpoint of the resource space
 */
int pd_add_rde(pd_t *pd,
               rde_type_t type,
               uint32_t space_id,
               seL4_CPtr server_ep);

/**
 * @brief Send a cap to a PD's cspace
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
 * bad functions that are only here because of our cursed sel4utils struct dependencies
 * for setting fields in the sel4utils_process_t struct from outside the PD component
 */
void pd_proc_set_page_dir(pd_t *pd, ads_t *target_ads);

void pd_proc_set_thread(pd_t *pd, cpu_t *target_cpu);
