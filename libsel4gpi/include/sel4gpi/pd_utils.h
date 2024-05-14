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

typedef struct _sel4gpi_process
{
    pd_client_context_t pd;
    ads_client_context_t ads;
    cpu_client_context_t cpu;
    int stack_pages;
    void *stack;
    void *entry_point;
} sel4gpi_process_t;

// holds the components that make up one PD
typedef struct _sel4gpi_pd
{
    pd_client_context_t pd;
    ads_client_context_t ads;
    cpu_client_context_t cpu;
} sel4gpi_pd_t;

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
 * @brief (WIP - to be removed) configures a process using only osmosis framework functions. By default, shares the MO RDE with the process.
 *
 * @param image_name
 * @param stack_pages number of pages for the stack
 * @param heap_pages number of pages for the heap
 * @param argc
 * @param args
 * @param ret_proc OPTIONAL, returns a struct containing the pd, ads, and cpu objects associated with the process
 * @return 0 on success, -1 on failure
 */
int sel4gpi_configure_process(const char *image_name,
                              int stack_pages,
                              int heap_pages,
                              sel4gpi_process_t *ret_proc);

/**
 * @brief (WIP - to be removed) spawns the given process
 *
 * @param proc a populated sel4gpi_process_t struct
 * @return int
 */
int sel4gpi_spawn_process(sel4gpi_process_t *proc, int argc, seL4_Word *args);

/**
 * @brief configures a dst_pd and prepares it for execution (via cpu_start)
 *
 * @param cfg the configuration of resources to follow, NOTE: currently does not prevent invalid configurations
 * @param src_pd the PD to base configurations on, currently unused
 * @param dst_pd the PD to be configured
 * @param argc the number of arguments to pass to the PD
 * @param args the arguments
 * @param ret_pd returns the necessary components making up the PD
 * @return int returns 0 on success, 1 on failure
 */
int sel4gpi_configure_pd(pd_resource_config_t *cfg, pd_client_context_t *src_pd, pd_client_context_t *dst_pd, int argc, seL4_Word *args, sel4gpi_pd_t *ret_pd);

/* helpers to get commonly used PD configurations */
/**
 * @brief generates a PD configuration that describes a process
 *
 * @param image_name the name of the process's image
 * @return pd_resource_config_t* returns a filled in config struct, caller is responsbile for freeing
 */
pd_resource_config_t *sel4gpi_generate_proc_config(char *image_name);
