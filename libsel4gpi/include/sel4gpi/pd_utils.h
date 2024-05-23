/**
 * Defines some utility functions for osmosis PDs
 */

#include <sel4gpi/badge_usage.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/cpu_clientapi.h>
#include <stdint.h>
#include <sel4/types.h>
#include <sel4gpi/badge_usage.h>

#define PD_HEAP_LOC 0x5000000000
#define PD_CAP_ROOT SEL4UTILS_CNODE_SLOT
#define PD_CAP_DEPTH seL4_WordBits
#define PD_CSPACE_SIZE_BITS 17

/*
 * Get the current PD's connection object from the env
 */
pd_client_context_t sel4gpi_get_pd_conn(void);

/*
 * Get the current PD's bound ADS connection object from the env
 */
ads_client_context_t sel4gpi_get_ads_conn(void);

/*
 * Get the current PD's CPU connection object from the env
 */
cpu_client_context_t sel4gpi_get_cpu_conn(void);

/**
 * Get the ID of the currently bound ADS
*/
uint64_t sel4gpi_get_binded_ads_id(void);

/*
 * Get the cspace root cap from the env
 */
seL4_CPtr sel4gpi_get_cspace_root(void);

// (XXX) Arya: TODO, modify the "get rde" functions to return a connection object

/**
 * Get an osmosis RDE from the env
 * Returns the default resource space for the provided type
 * 
 * @param type gpi_cap_t type of the RDE
 */
seL4_CPtr sel4gpi_get_rde(int type);

/**
 * Get an osmosis RDE from the env
 * Tries to find the RDE for the given type and resource space id
 *
 * @param space_id resource space ID to find
 * @param type type of the RDE
 * @return null cap if the RDE cannot be found
 */
seL4_CPtr sel4gpi_get_rde_by_space_id(uint32_t space_id, gpi_cap_t type);

/**
 * Set the exit callback to the default GPI exit handler
 */
void sel4gpi_set_exit_cb(void);

/**
 * @brief obtains a new VMR by requesting an MO and then attaching it to the given ADS
 *
 * @param ads_rde the ADS which the VMR belongs to
 * @param num_pages number of pages
 * @param vaddr OPTIONAL, address in which the VMR should be mapped
 * @param vmr_type type of VMR, e.g. stack, heap, IPC buffer, etc.
 * @param ret_mo OPTIONAL, returns a reference to the MO object for this VMR
 * @return virtual address of the VMR, if vaddr argument is specified, it should be the same (or NULL, on failure)
 */
void *sel4gpi_get_vmr(ads_client_context_t *ads_rde, int num_pages, void *vaddr, sel4utils_reservation_type_t vmr_type, mo_client_context_t *ret_mo);

/**
 * @brief creates a new stack with num_pages in the given ADS, it will NOT be mapped to the current one.
 *        NOTE: no guard page is created
 * @param ads the ADS in which to create the stack
 * @param n_pages number of pages for the stack
 * @return the top of the stack in the given ADS (NOT the current one)
 */
void *sel4gpi_new_sized_stack(ads_client_context_t *ads, size_t n_pages);
