#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/pd_obj.h>
#include <sel4gpi/test_init_data.h>
#include <sel4gpi/resource_component_utils.h>
#include <sel4gpi/resource_space_clientapi.h>
#include <pd_component_rpc.pb.h>

/** @file APIs for managing and interacting with the serial server thread.
 *
 * Defines the constants for the protocol, messages, and server-side state, as
 * well as the entry point and back-end routines for the server's API.
 *
 * All vka_t, vspace_t and simple_t instances that are supplied to this library
 * by the developer must persist and be functional for the lifetime of the
 * server thread.
 */

#define PDSERVS "PDServ Component: "
#define PDSERVC "PDServ Client   : "

#define PD_TERMINATED_CODE 127

// Data to send when a PD requests work
typedef struct _pd_work_entry
{
    gpi_res_id_t res_id;   ///< Identifier of the resource the work is for
    uint32_t client_pd_id; ///< Identifier of the PD the work is for
                           ///< For model extraction: The client PD that held the resource we are extracting
                           ///< For resource free: Currently unused
} pd_work_entry_t;

// Registry of PDs maintained by the server
typedef struct _pd_component_registry_entry
{
    resource_registry_node_t gen;
    pd_t pd;

    /* Pending Work */
    linked_list_t *pending_frees;       ///< List of pd_work_entry_t for resources to free from a resource space
                                        ///< that this PD manages
    linked_list_t *pending_destroy;     ///< List of pd_work_entry_t for resources to destroy from a resource space
                                        ///< that this PD manages
    linked_list_t *pending_model_state; ///< List of pd_work_entry_t for resources for which we need the model
                                        ///< state from this PD
} pd_component_registry_entry_t;

/**
 * To initialize the pd component at the beginning of execution
 */
int pd_component_initialize(vka_t *server_vka,
                            vspace_t *server_vspace,
                            vka_object_t server_ep_obj);

/* Global server instance accessor functions. */
resource_component_context_t *get_pd_component(void);

// Creates a dummy PD object for the root task
void forge_pd_for_root_task(uint64_t rt_id);

/**
 * Add a resource that the PD holds in metadata only, the resource isn't actually minted into the PD's cspace
 *
 * @param pd_id the ID of the PD to add a resource to
 * @param res_id the resource to add
 * @param slot_in_RT for debugging purposes, may be removed
 * @param slot_in_PD for debugging purposes, may be removed
 * @param slot_in_serverPD for debugging purposes, may be removed
 * @return 0 on success, 1 otherwise
 */
int pd_add_resource_by_id(uint32_t pd_id,
                          gpi_res_id_t res_id,
                          seL4_CPtr slot_in_RT,
                          seL4_CPtr slot_in_PD,
                          seL4_CPtr slot_in_serverPD);

#if TRACK_MAP_RELATIONS
/***
 * Map one resource to another
 * (XXX) Arya: At the moment this does nothing but check that the mapping is valid
 *
 * @param client_pd_id ID of the PD that is requesting the mapping
 * @param src_res_id the universal ID of the source resource
 * @param dest_res_id the universal ID of the destination resource
 * @return 0 on success, error otherwise
 */
int pd_component_map_resources(uint32_t client_pd_id, uint64_t src_res_id, uint64_t dest_res_id);
#endif

/**
 * To be called after a core resource is destroyed, since the root task does not count as a refcount
 * Remove it from the root task's metadata
 *
 * @param res_id ID of the deleted resource
 * @return 0 on success, error otherwise
 */
int pd_component_remove_resource_from_rt(gpi_res_id_t res_i);

/**
 * To be called when a resource is destroyed
 * Remove the resource from any PDs that may hold it
 *
 * @param res_id ID of the deleted resource
 * @return 0 on success, error otherwise
 */
int pd_component_resource_cleanup(gpi_res_id_t res_id);

/**
 * To be called when a resource space is destroyed
 *
 * @param pd_id ID of the PD that manages this space
 * @param space_type type of the deleted resource space
 * @param space_id ID of the deleted resource space
 * @param execute_cleanup_policy if true, execute a cleanup policy for any PDs that still depend on the resource space
 * @return 0 on success, error otherwise
 */
int pd_component_space_cleanup(uint32_t pd_id, gpi_cap_t space_type, uint32_t space_id, bool execute_cleanup_policy);

/**
 * Get a PD from the registry by ID
 *
 * @param object_id the PD ID
 * @return the PD's registry entry, or NULL if not found
 */
pd_component_registry_entry_t *pd_component_registry_get_entry_by_id(seL4_Word object_id);

/**
 * Get a PD from the registry by badge
 *
 * @param badge the PD resource badge
 * @return the PD's registry entry, or NULL if not found
 */
pd_component_registry_entry_t *pd_component_registry_get_entry_by_badge(seL4_Word badge);

/**
 * Queue some model extraction work for the PD to complete
 * The work must be part of a currently pending model state extraction task
 *
 * @param pd_entry the target PD
 * @param work the details of the work
 */
void pd_component_queue_model_extraction_work(pd_component_registry_entry_t *pd_entry, pd_work_entry_t *work);

/**
 * Queue a resource for a resource server to free
 * The resource server is thus notified that another PD has stopped holding one of its resources
 * The server may or may not decide to destroy the resource / add it back to a pool
 * 
 * @param pd_entry the target PD
 * @param work the details of the work
 */
void pd_component_queue_free_work(pd_component_registry_entry_t *pd_entry, pd_work_entry_t *work);

/**
 * Queue a resource / space for a resource server to destroy
 * The resource server will be responsible to clean up any metedata / dependencies
 * of the deleted resource / space
 *
 * @param pd_entry the target PD
 * @param work the details of the work
 */
void pd_component_queue_destroy_work(pd_component_registry_entry_t *pd_entry, pd_work_entry_t *work);

/**
 * Allocate a PD from the root task
 *
 * @param client_id the PD id of the client requesting the ADS
 * @param init_mo an MO to use for the PD's init data
 * @param ret_ads returns the created PD
 * @param ret_cap returns the slot of the new PD, in the client (or NULL, to make no cap)
 */
int pd_component_allocate(uint32_t client_id, mo_t *mo, pd_t **ret_pd, seL4_CPtr *ret_cap);

/**
 * @brief
 * P repares the (PD, ADS, CPU) combination with the given arguments,
 *        entry point, stack, and IPC buffer, and OSmosis data frame
 * This eventually will be removed in favour of a unified PD entry-point
 * TODO Linh: better explain what differs between setup types
 *
 * @param target_pd the target PD to initialize
 * @param target_ads the ADS where the stack resides
 * @param target_cpu the CPU which will execute in this ADS and PD
 * @param setup_type the type of setup (see PdSetupType for details)
 * @param argc the number of arguments to place on the stack
 * @param args the arguments, as seL4_Word values
 * @param stack_top pointer to a position in the stack, in the target ADS, depends on the setup type
 * @param entry_point the address of the instruction to start executing at (in the target ADS)
 * @param ipc_buf_addr the address of the IPC buffer for the (PD, ADS, CPU) combination
 * @param osm_shared_data address of the OSmosis data frame within the target ADS
 * @return int 0 on success
 */
int pd_component_runtime_setup(pd_t *pd,
                               ads_t *ads,
                               cpu_t *cpu,
                               PdSetupType setup_mode,
                               int argc,
                               seL4_Word *args,
                               void *stack_top,
                               void *entry_point,
                               void *ipc_buf_addr,
                               void *osm_shared_data);

/**
 * Terminate a PD by ID, from the root task
 *
 * @param pd_id the ID of the PD to terminate
 * @return 0 on success, error otherwise
 */
int pd_component_terminate(uint32_t pd_id);