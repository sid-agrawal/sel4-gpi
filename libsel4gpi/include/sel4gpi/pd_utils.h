/**
 * Defines some utility functions for osmosis PDs
 */

#include <sel4/types.h>

/*
 * Get the osmosis pd cap from the env
 */
seL4_CPtr sel4gpi_get_pd_cap(void);

/*
 * Get the osmosis ads cap from the env
 */
seL4_CPtr sel4gpi_get_ads_cap(void);

/*
 * Get an osmosis RDE from the env
 * @param type gpi_cap_t type of the RDE
 */
seL4_CPtr sel4gpi_get_rde(int type);

uint64_t sel4gpi_get_binded_ads_id(void);

/**
 * @brief finds an RDE given its NS ID and type
 *
 * @param ns_id
 * @param type
 * @return null cap if the RDE cannot be found
 */
seL4_CPtr sel4gpi_get_rde_by_ns_id(uint32_t ns_id, gpi_cap_t type);
