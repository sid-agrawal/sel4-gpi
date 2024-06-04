#pragma once
/**
 * @file pd_creation.h
 * @author Linh Pham (phamhlinh01@gmail.com)
 * @brief Describes configuration types for creating new PDs based on a source PD
 *        Each configuration defines the level of sharing between the source PD and the new PD
 * @version 0.1
 * @date 2024-05-27
 *
 * @copyright Copyright (c) 2024
 *
 */

#include <stdint.h>
#include <sel4/types.h>
#include <utils/ansi_color.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/linked_list.h>

#define DEFAULT_STACK_PAGES 16
#define DEFAULT_HEAP_PAGES 100

#define PD_CREATION_DBG 1

#ifdef PD_CREATION_DBG
#define PD_CREATION_PRINT(msg, ...)                                     \
    do                                                                  \
    {                                                                   \
        printf(COLORIZE("%s():\t", BLUE) msg, __func__, ##__VA_ARGS__); \
    } while (0)
#else
#define PD_CREATION_PRINT(...)
#endif // PD_CREATION_DBG

/**
 * @brief holds the components for a runnable entity
 */
typedef struct _sel4gpi_runnable
{
    pd_client_context_t pd;
    ads_client_context_t ads;
    cpu_client_context_t cpu;
} sel4gpi_runnable_t;

/**
 * @brief describes sharing between 2 PDs
 */
typedef enum _gpi_share_degree
{
    /** this resource is directly shared with the other PD, e.g. virt pages that map to the same phys page */
    GPI_SHARED = 1,
    /** this resource is copied into the other PD, e.g. virt pages with separate phys pages with contents copied */
    GPI_COPY,
    /** this resource exists in the other PD, but has no relation with the source PD */
    GPI_DISJOINT,
    /** this resource will not exist in the other PD*/
    GPI_OMIT
} gpi_share_degree_t;

/**
 * @brief configuration of a particular VMR, do not use for the stack and ELF regions
 */
typedef struct _vmr_config
{
    gpi_share_degree_t share_mode;
    sel4utils_reservation_type_t type;
    /** vaddr to start of the VMR */
    void *start;
    /** number of pages in this VMR */
    uint64_t region_pages;
} vmr_config_t;

/**
 * @brief Configuration of an entire ADS, the type of sharing is w.r.t. the current ADS
 */
typedef struct _ads_config
{
    /** only used if code_shared == GPI_DISJOINT */
    const char *image_name;
    /** if specified, will take precedence over any automatically found ones */
    void *entry_point;

    /** if we're in the same ADS, configuring any of these as GPI_SHARED has no effect
     * (XXX) Linh: these are to be removed once we have a unified entry point,
     *             this should match up with the sharing degree specified inside vmr_cfgs
     */
    gpi_share_degree_t code_shared;
    gpi_share_degree_t stack_shared;
    size_t stack_pages;

    /** list of vaddrs to non-contiguous VMRs to configure, the heap should be specified here
     *  if we're in the same ADS, configuring any of these as GPI_SHARED has no effect
     */
    linked_list_t *vmr_cfgs;
} ads_config_t;

/**
 * @brief defines what RDE type, and which resource space to share with the new PD
 */
typedef struct _rde_config
{
    gpi_cap_t type;
    uint32_t space_id;
} rde_config_t;

/**
 * @brief For creating new PDs: defines the level of sharing between a given source PD and the new PD
 */
typedef struct _pd_config
{
    /** supply a fault-endpoint for the PD, if NULL, will create a new one */
    seL4_CPtr fault_ep;
    mo_client_context_t osm_data_mo;
    ads_config_t ads_cfg;
    linked_list_t *rde_cfg;
    linked_list_t *gpi_res_type_cfg;
    // ongoing: add configs for other resources here as needed
} pd_config_t;

/**
 * @brief creates a new PD, ADS, CPU and generates a configuration that can start a process
 *
 * @param image_name ELF image for the process
 * @param stack_pages size of stack, in pages
 * @param heap_pages size of heap, in pages
 * @param ret_runnable returns a filled in runnable with the PD, ADS, CPU contexts
 * @return pd_config_t* returns the PD configuration, NULL on failure. Caller is responsbile for freeing the config.
 */
pd_config_t *sel4gpi_configure_process(const char *image_name,
                                       int stack_pages,
                                       int heap_pages,
                                       sel4gpi_runnable_t *ret_runnable);

/**
 * @brief creates a new PD and CPU, and generates a configuration that can start a thread
 *
 * @param thread_fn the function the thread will run
 * @param fault_ep OPTIONAL a fault endpoint for the thread, if not provided, a new one will be allocated
 * @param ret_runnable returns a filled in runnable with the PD, ADS, CPU contexts
 *                     the ADS context returned will be the exact same as the current ADS
 * @return pd_config_t* returns the PD configuration, NULL on failure. Caller is responsbile for freeing the config.
 */
pd_config_t *sel4gpi_configure_thread(void *thread_fn, seL4_CPtr fault_ep, sel4gpi_runnable_t *ret_runnable);

/**
 * @brief configures a runnable entity given a created PD and prepares it for execution (via cpu_start)
 *
 * @param cfg the configuration of resources to follow, NOTE: currently does not prevent invalid configurations
 * @param runnable a runnable struct with only the PD context populated, the ADS and CPU contexts will be populated on return
 * @param argc the number of arguments to pass to the PD
 * @param args the arguments
 * @return int returns 0 on success, 1 on failure
 */
int sel4gpi_start_pd(pd_config_t *cfg, sel4gpi_runnable_t *runnable, int argc, seL4_Word *args);

/* helpers to get commonly used PD configurations */
/**
 * @brief generates a PD configuration that describes a process
 *
 * @param image_name the name of the process's image
 * @param stack_pages size of the stack, in pages
 * @param heap_pages size of the heap, in pages
 * @param osm_data_mo the MO for holding OSmosis data
 * @return pd_config_t* returns a filled in config struct, caller is responsbile for freeing
 */
pd_config_t *sel4gpi_generate_proc_config(const char *image_name, size_t stack_pages,
                                          size_t heap_pages, mo_client_context_t *osm_data_mo);

/**
 * @brief generates a PD configuration that describes a thread
 *
 * @param thread_fn the thread's entry function
 * @param fault_ep the fault endpoint for the thread (OPTIONAL, if not specified, a new one will be allocated)
 * @param osm_data_mo the MO for holding OSmosis data
 * @return pd_config_t*
 */
pd_config_t *sel4gpi_generate_thread_config(void *thread_fn, seL4_CPtr fault_ep, mo_client_context_t *osm_data_mo);

/**
 * @brief frees all memory used by a config and the config itself
 *
 * @param cfg the config to destroy
 */
void sel4gpi_config_destroy(pd_config_t *cfg);

/**
 * @brief convert a gpi_share_degree_t to human-readable string
 *
 * @param share_deg the share degree type
 * @return char* string representation of the sharing degree
 */
char *sel4gpi_share_degree_to_str(gpi_share_degree_t share_deg);

/**
 * @brief set up an ADS given the configuration. Can be used as a standalone to configure a new ADS for an existing PD.
 *
 * @param cfg The ADS config options
 * @param runnable an allocated runnable struct that can be empty, but typically with the PD context filled in
 * @param osm_data_mo OPTIONAL: an MO for holding OSmosis data
 * @param ret_stack OPTIONAL: address of the allocated stack
 * @param ret_ipc_buf OPTIONAL: address of the allocated IPC buffer
 * @param ret_entry_point OPTIONAL: address of the entry point, if found. If an entry point is specified in the config,
 *                        this will be the same
 * @param ret_osm_data OPTIONAL: address of the mapped OSmosis data frame (only applies if osm_data_mo is given)
 * @param ret_ipc_buf_mo OPTIONAL: the MO for the IPC buffer
 * @return int 0 on success
 */
int sel4gpi_ads_configure(ads_config_t *cfg,
                          sel4gpi_runnable_t *runnable,
                          mo_client_context_t *osm_data_mo,
                          void **ret_stack,
                          void **ret_ipc_buf,
                          void **ret_entry_point,
                          void **ret_osm_data,
                          mo_client_context_t *ret_ipc_buf_mo);
