/**
 * Defines some utility functions for osmosis PDs
 */

#include <sel4gpi/badge_usage.h>
#include <stdint.h>
#include <sel4/types.h>

#define PD_HEAP_LOC 0x5000000000

/*
 * Get the osmosis pd cap from the env
 */
seL4_CPtr sel4gpi_get_pd_cap(void);

/*
 * Get the osmosis ads cap from the env
 */
seL4_CPtr sel4gpi_get_ads_cap(void);

/*
 * Get the osmosis cpu cap from the env
 */
seL4_CPtr sel4gpi_get_cpu_cap(void);

/*
 * Get the cspace root cap from the env
 */
seL4_CPtr sel4gpi_get_cspace_root(void);

/*
 * Get an osmosis RDE from the env
 * @param type gpi_cap_t type of the RDE
 */
seL4_CPtr sel4gpi_get_rde(int type);

/**
 * Get the ID of the currently bound ADS
*/
uint64_t sel4gpi_get_binded_ads_id(void);

/**
 * @brief finds an RDE given its NS ID and type
 *
 * @param ns_id
 * @param type
 * @return null cap if the RDE cannot be found
 */
seL4_CPtr sel4gpi_get_rde_by_ns_id(uint32_t ns_id, gpi_cap_t type);

/**
 * Set the exit callback to the default GPI exit handler
*/
void sel4gpi_set_exit_cb(void);