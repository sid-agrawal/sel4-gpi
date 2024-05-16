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
#include <sel4gpi/resource_space_clientapi.h>

#define PD_HEAP_LOC 0x5000000000
#define DEFAULT_STACK_PAGES 16
#define DEFAULT_HEAP_PAGES 100
#define PD_CAP_ROOT SEL4UTILS_CNODE_SLOT
#define PD_CAP_DEPTH seL4_WordBits
#define PD_CSPACE_SIZE_BITS 17

#define PD_UTIL_DBG 1

#ifdef PD_UTIL_DBG
#define PD_UTIL_PRINT(msg, ...)                                  \
    do                                                           \
    {                                                            \
        printf("PD UTILS %s():\t" msg, __func__, ##__VA_ARGS__); \
    } while (0)
#else
#define PD_UTIL_PRINT(...)
#endif // PD_UTIL_DBG

// holds the components for a runnable entity
typedef struct _sel4gpi_runnable
{
    pd_client_context_t pd;
    ads_client_context_t ads;
    cpu_client_context_t cpu;
} sel4gpi_runnable_t;

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

/**
 * @brief creates a new PD, and generates a configuration that can start a process
 * by default, gives the new process the MO and RESSPC RDE
 *
 * @param image_name ELF image for the process
 * @param stack_pages size of stack, in pages
 * @param heap_pages size of heap, in pages
 * @param ret_pd returns the newly created PD context
 * @return pd_resource_config_t* returns the PD configuration, NULL on failure. Caller is responsbile for freeing the config.
 */
pd_resource_config_t *sel4gpi_configure_process(const char *image_name, int stack_pages, int heap_pages, pd_client_context_t *ret_pd);

/**
 * @brief configures a runnable entity given a created PD and prepares it for execution (via cpu_start)
 *
 * @param cfg the configuration of resources to follow, NOTE: currently does not prevent invalid configurations
 * @param runnable a runnable struct with only the PD context populated, the ADS and CPU contexts will be populated on return
 * @param argc the number of arguments to pass to the PD
 * @param args the arguments
 * @return int returns 0 on success, 1 on failure
 */
int sel4gpi_start_pd(pd_resource_config_t *cfg, sel4gpi_runnable_t *runnable, int argc, seL4_Word *args);

/* helpers to get commonly used PD configurations */
/**
 * @brief generates a PD configuration that describes a process
 *
 * @param image_name the name of the process's image
 * @return pd_resource_config_t* returns a filled in config struct, caller is responsbile for freeing
 */
pd_resource_config_t *sel4gpi_generate_proc_config(char *image_name, size_t stack_pages, size_t heap_pages);

/**
 * @brief (WIP)
 *
 * @return pd_resource_config_t*
 */
pd_resource_config_t *sel4gpi_generate_thread_config(void);
