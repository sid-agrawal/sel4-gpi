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

/**
 * @brief sets up a TCB's stack so that it can run a thread, specifically the TLS
 * this is was copied over from libsel4utils
 *
 * @param stack_addr top of stack in current vspace
 * @param stack_pages number of pages for the stack
 * @return uintptr_t pointer to top of a set up stack
 */
uintptr_t sel4gpi_setup_thread_stack(void *stack_addr, size_t stack_pages);
