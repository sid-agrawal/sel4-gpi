/**
 * Defines some utility functions for osmosis PDs
 */

#include <stdint.h>
#include <sel4/types.h>

#include <sel4gpi/badge_usage.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/cpu_clientapi.h>
#include <sel4gpi/endpoint_clientapi.h>

/* We define a specific address for our static heap since we want the flexibility of allocating an MO for it
 * (rather than it being part of the ELF data) */
#define PD_HEAP_LOC 0x5000000000
#define PD_CAP_ROOT SEL4UTILS_CNODE_SLOT
#define PD_CAP_DEPTH seL4_WordBits
#define PD_CSPACE_SIZE_BITS 17

/* OSmosis data on the TLS */
extern __thread void *__sel4gpi_osm_data;

/** FUNCTIONS TO INTERACT WITH OSMOSIS SHARED DATA **/

/**
 * Get the location of this PD's shared data frame
 */
osm_pd_shared_data_t *sel4gpi_get_shared_data(void);

/*
 * Get the current PD's connection object from the env
 */
pd_client_context_t sel4gpi_get_pd_conn(void);

/*
 * Get the current PD's bound ADS connection object from the env
 * 
 * This connection is used to send the ADS as a resource to another PD
 * It cannot be used to request VMR regions
 */
ads_client_context_t sel4gpi_get_ads_conn(void);

/*
 * Get the current PD's CPU connection object from the env
 */
cpu_client_context_t sel4gpi_get_cpu_conn(void);

/**
 * Get the ID of the currently bound ADS
 */
gpi_obj_id_t sel4gpi_get_binded_ads_id(void);

/*
 * Get the cspace root cap from the env
 */
seL4_CPtr sel4gpi_get_cspace_root(void);

/**
 * Get a resource type code from the resource type name
 *
 * @param type_name the text name of the resource type
 */
gpi_cap_t sel4gpi_get_resource_type_code(char *type_name);

/**
 * Get a resource type name from the resource type code
 * This only works if the resource type is in the PD's RDE
 *
 * @param type the text name of the resource type
 */
char *sel4gpi_get_resource_type_name(gpi_cap_t type);

/**
 * Get an osmosis RDE from the env
 * Returns the default resource space for the provided type
 *
 * @param type gpi_cap_t type of the RDE
 * @return the cptr to the RDE endpoint
 */
seL4_CPtr sel4gpi_get_rde(int type);

/**
 * Get the VMR RDE for this PD's currently-bound ADS
 * 
 * Use this connection to request new VMR regions from the ADS
 */
ads_client_context_t sel4gpi_get_bound_vmr_rde(void);

/**
 * Get the resource space ID of the default RDE for the given type
 *
 * @param type gpi_cap_t type of the RDE
 * @return the resource space ID of the default RDE,
 *         or 0 if there is none for the given type
 */
gpi_obj_id_t sel4gpi_get_default_space_id(int type);

/**
 * Get an osmosis RDE from the env
 * Tries to find the RDE for the given type and resource space id
 * If the given space id == RESSPC_ID_NULL, this is identical to calling sel4gpi_get_rde(type)
 *
 * @param space_id resource space ID to find
 * @param type type of the RDE
 * @return null cap if the RDE cannot be found
 */
seL4_CPtr sel4gpi_get_rde_by_space_id(gpi_space_id_t space_id, gpi_cap_t type);

/**
 * @brief print all RDEs for debugging
 *
 */
void sel4gpi_debug_print_rde(void);

/**
 * For a resource manager to store a copy of the reply cap.
 * This should be done immediately after receiving a message.
 * 
 * This function will not make any IPC calls.
*/
void sel4gpi_store_reply_cap(void);

/**
 * For a resource manager to retrieve a copy of the reply cap.
*/
seL4_CPtr sel4gpi_get_reply_cap(void);

/**
 * For a resource manager to clear the reply ap.
 * This should be called when a request is complete.
 * 
 * This function will make IPC calls to the root task to clear / reallocate the reply cap slot
*/
void sel4gpi_clear_reply_cap(void);

/** OTHER UTIL FUNCTIONS **/

/**
 * Set the exit callback to the default GPI exit handler
 */
void sel4gpi_set_exit_cb(void);

/**
 * @brief obtains a new VMR by requesting an MO and then attaching it to the given ADS
 *
 * @param vmr_rde the ADS which the VMR belongs to
 * @param num_pages number of pages
 * @param vaddr OPTIONAL, address in which the VMR should be mapped
 * @param vmr_type type of VMR, e.g. stack, heap, IPC buffer, etc.
 * @param[out] ret_mo OPTIONAL, returns a reference to the MO object for this VMR
 * @return virtual address of the VMR, if vaddr argument is specified, it should be the same (or NULL, on failure)
 */
void *sel4gpi_get_vmr(ads_client_context_t *vmr_rde,
                      int num_pages,
                      void *vaddr,
                      sel4utils_reservation_type_t vmr_type,
                      size_t page_bits,
                      mo_client_context_t *ret_mo);

/**
 * @brief obtains a new VMR for an MO at a specific physical address.
 * For identity mapping, specify vaddr to be the same as paddr
 *
 * @param vmr_rde the ADS which the VMR belongs to
 * @param num_pages number of pages
 * @param vaddr OPTIONAL, address in which the VMR should be mapped
 * @param vmr_type type of VMR, e.g. stack, heap, IPC buffer, etc.
 * @param size_t page_bits size of an individual page
 * @param paddr the phys address of of the MO
 * @param[out] ret_mo OPTIONAL, returns a reference to the MO object for this VMR
 * @return virtual address of the VMR, if vaddr argument is specified, it should be the same (or NULL, on failure)
 */
void *sel4gpi_get_vmr_at_paddr(ads_client_context_t *vmr_rde,
                               int num_pages,
                               void *vaddr,
                               sel4utils_reservation_type_t vmr_type,
                               size_t page_bits,
                               uintptr_t paddr,
                               mo_client_context_t *ret_mo);

/**
 * Unattach an MO from the given ADS then destroy it
 *
 * @param vmr_rde the ADS where the MO is attached
 * @param vaddr the vaddr where the MO is attached
 * @param mo connection to the MO to destroy
 * @return 0 on success, error otherwise
 */
int sel4gpi_destroy_vmr(ads_client_context_t *vmr_rde, void *vaddr, mo_client_context_t *mo);

/**
 * @brief allocates a new tracked endpoint using the default EP RDE
 *
 * @param[out] ret_ep_conn empty connection context to fill in
 * @return int 0 on success, 1 on failure
 */
int sel4gpi_alloc_endpoint(ep_client_context_t *ret_ep_conn);

/**
 * @brief copies size_bytes at the given vaddr to a destination MO by attaching it to the current ADS,
 * then detaching the MO after copying has completed
 *
 * @param vaddr address of region to copy
 * @param size_bytes total size of region to copy in bytes
 * @param dest_mo the MO to copy data into
 * @return int 0 on success, 1 on failure
 */
int sel4gpi_copy_data_to_mo(void *vaddr, size_t size_bytes, mo_client_context_t *dest_mo);

/**
 * @brief debug print of the contents of memory starting at the given virtual address
 * referenced from the Barrelfish OS project
 *
 * @param start_addr the virtual address to dump memory contents of
 * @param range byte range to dump
 */
void debug_print_mem_at(void *start_addr, uint32_t range);
