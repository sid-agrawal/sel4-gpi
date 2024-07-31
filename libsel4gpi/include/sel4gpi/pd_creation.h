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
#include <sel4gpi/endpoint_clientapi.h>
#include <sel4gpi/ads_client_context.h>
#include <sel4gpi/mo_client_context.h>
#include <sel4gpi/pd_client_context.h>
#include <sel4gpi/cpu_client_context.h>

#define DEFAULT_STACK_PAGES 16
#define DEFAULT_HEAP_PAGES 100

#define PD_CREATION_DBG 1

#ifdef PD_CREATION_DBG
#define PD_CREATION_PRINT(msg, ...)                                     \
    do                                                                  \
    {                                                                   \
        printf(COLORIZE("%s():\t", BLUE) msg, __func__, ##__VA_ARGS__); \
    } while (0)

#define PD_CREATION_PRINT_2(msg, ...) \
    do                                \
    {                                 \
        printf(msg, ##__VA_ARGS__);   \
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
    GPI_SHARED = 1, ///< this resource is directly shared with the other PD,
                    ///< e.g. virt pages that map to the same phys page
    GPI_DISJOINT,   ///< this resource exists in the other PD, but has no relation with the source PD
} gpi_share_degree_t;

/**
 * @brief configuration of a particular VMR
 *
 * Sharing mode dictates which options are optional and/or ignored
 *
 * +--------------+------------+--------------+-----------------------------------------+
 * |    Option    | GPI_SHARED | GPI_DISJOINT |         Default (when optional)         |
 * +--------------+------------+--------------+-----------------------------------------+
 * | type         | required   | required     |                                         |
 * | start        | required   | optional     | any available vaddr                     |
 * | dest_start   | optional   | ignored      | `start`                                 |
 * | region_pages | required^1 | required     |                                         |
 * | page_bits    | ignored    | optional     | 4K pages (ARM specific)                 |
 * | mo           | ignored    | optional     | new MO will be allocated                |
 * +--------------+------------+--------------+-----------------------------------------+
 * 1 = optional if type != SHARED_FRAMES or GENERIC, the VMR will be searched
 *     for by type, and info will be taken from the found VMR
 */
typedef struct _vmr_config
{
    gpi_share_degree_t share_mode;     ///< the type of sharing between the current ADS ands the configured,
                                       ///< dictates which of the below are optional or ignored,
                                       ///< see table above for reference
    sel4utils_reservation_type_t type; ///< type of VMR
    void *start;                       ///< vaddr to start of the VMR in the src ADS
    void *dest_start;        ///< OPTIONAL vaddr to the start of VMR in the destination ADS
    uint64_t region_pages;   ///< number of pages in this VMR
    size_t page_bits;        ///< OPTIONAL size of an individual page in this VMR, 4K pages by default
    mo_client_context_t mo;  ///< OPTIONAL an MO to use to map the VMR
} vmr_config_t;

/**
 * @brief holds the various info a PD depends on for execution, includeing runtime addresses
 * and MOs corresponding to special VMRs
 */
typedef struct _runtime_context
{
    void *entry_point;              ///< address to PD entry point
    void *osm_data;                 ///< address to PD's OSmosis data
    vmr_config_t *stack_cfg;        ///< VMR config describing the stack region
    vmr_config_t *ipc_buf_cfg;      ///< VMR config describing the IPC buffer region
    bool loaded_elf; ///< ELF was loaded for this runtime
} runtime_context_t;

/**
 * @brief Configuration of an entire ADS, the type of sharing is w.r.t. the current ADS
 * Although this is an ADS configuration, it is combined with MO sharing.
 * Caller can provide an MO which will both be shared with the created PD and mapped
 * to the specified VMR
 */
typedef struct _ads_config
{
    const char *image_name; ///< only used if ADS config contains a CODE region that's GPI_DISJOINT
    void *entry_point;      ///< if specified, will take precedence over any automatically found ones

    /** list of vaddrs to non-contiguous VMRs to configure
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
    gpi_space_id_t space_id;
} rde_config_t;

/**
 * @brief For creating new PDs: defines the level of sharing between a given source PD and the new PD
 */
typedef struct _pd_config
{
    ep_client_context_t fault_ep;    ///< supply a tracked fault-endpoint for the PD, if NULL, will create a new one
    mo_client_context_t osm_data_mo; ///< the MO for holding a PD's OSmosis data
    ads_config_t ads_cfg;            ///< the ADS config
    linked_list_t *rde_cfg;          ///< the RDE config
    linked_list_t *gpi_res_type_cfg; ///< a list of GPICAP_TYPE_X resource types,
                                     ///< all resources of this type will be shared with the PD
                                     ///< currently does not differentiate between resource spaces
    bool elevated_cpu;               ///< whether the CPU should have elevated privileges
    // ongoing: add configs for other resources here as needed
} pd_config_t;

/**
 * @brief creates components for exeuction: a new PD with an OSM shared data frame, and optionally new ADSes or CPUs.
 *
 * @param new_ads if true, creates a new ADS
 * @param new_cpu if true, creates a new CPU
 * @param[out] ret_runnable returns a filled in runnable with the PD, ADS, CPU contexts
 * @return pd_config_t* a mostly empty PD config, with only the OSmosis shared data MO filled in
 */
pd_config_t *sel4gpi_new_runnable(bool new_ads, bool new_cpu, sel4gpi_runnable_t *ret_runnable);

/**
 * @brief creates a new PD, ADS, CPU and generates a configuration that can start a process
 *
 * @param image_name ELF image for the process
 * @param stack_pages size of stack, in pages
 * @param heap_pages size of heap, in pages
 * @param[out] ret_runnable returns a filled in runnable with the PD, ADS, CPU contexts
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
pd_config_t *sel4gpi_configure_thread(void *thread_fn, ep_client_context_t *fault_ep, sel4gpi_runnable_t *ret_runnable);

/**
 * @brief configures a runnable entity given a created PD and prepares it for execution (via cpu_start)
 *
 * @param cfg the configuration of resources to follow, NOTE: currently does not prevent invalid configurations
 * @param runnable a runnable struct with only the PD context populated, the ADS and CPU contexts will be populated on return
 * @param argc the number of arguments to pass to the PD
 * @param args the arguments
 * @return int returns 0 on success, 1 on failure
 */
int sel4gpi_prepare_pd(pd_config_t *cfg, sel4gpi_runnable_t *runnable, int argc, seL4_Word *args);

/**
 * @brief start a prepared PD (via cpu_start)
 *
 * @param runnable a runnable struct already populated
 * @return int returns 0 on success, 1 on failure
 */
int sel4gpi_start_pd(sel4gpi_runnable_t *runnable);

/* helpers to get commonly used PD configurations */
/**
 * @brief populates a config with PD options that describe a process
 *
 * @param proc_cfg PD config to populate
 * @param image_name the name of the process's image
 * @param stack_pages size of the stack, in pages
 * @param heap_pages size of the heap, in pages
 */
void sel4gpi_generate_proc_config(pd_config_t *proc_cfg, const char *image_name, size_t stack_pages, size_t heap_pages);

/**
 * @brief populates a config with PD options that describes a thread
 *
 * @param thread_cfg PD config to populate
 * @param thread_fn the thread's entry function
 * @param fault_ep the tracked fault endpoint for the thread (OPTIONAL, if not specified, a new one will be allocated)
 */
void sel4gpi_generate_thread_config(pd_config_t *thread_cfg, void *thread_fn, ep_client_context_t *fault_ep);

/**
 * @brief adds all RDEs existing in the current PD to the given PD config
 *
 * @param cfg PD config to populate
 */
void sel4gpi_config_pd_share_all_rdes(pd_config_t *cfg);

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
 * The given cfg will be updated with any MOs that were allocated during the setup (excluding ELF MOs)
 *
 * @param cfg The ADS config options
 * @param runnable an allocated runnable struct that can be empty, but typically with the PD context filled in
 * @param osm_data_mo OPTIONAL: an MO for holding OSmosis data
 * @param ret_runtime_context OPTIONAL: returns a struct holding all the addresses a PD relies on for execution

 * @return int 0 on success
 */
int sel4gpi_ads_configure(ads_config_t *cfg,
                          sel4gpi_runnable_t *runnable,
                          mo_client_context_t *osm_data_mo,
                          runtime_context_t *ret_runtime_context);

/**
 * @brief Convenience function for configuring a new RDE to share with a PD (that's not yet started)
 * Will malloc a new RDE config, which gets freed during sel4gpi_config_destroy
 *
 * @param cfg an existing PD config for an unstarted PD
 * @param rde_type type of RDE to share
 * @param space_id resource space ID of the RDE
 */
void sel4gpi_add_rde_config(pd_config_t *cfg, gpi_cap_t rde_type, gpi_space_id_t space_id);

/**
 * @brief Convenience function for adding a new VMR config to an ADS config
 * Will malloc a new VMR config, which gets freed during sel4gpi_config_destroy
 *
 * see \ref vmr_config_t for more details on the listed params
 * and \ref ads_config_t for a higher-level explanation of ADS configuration
 *
 * @param cfg the ADS config, if no VMR config list was already created, will make a new one
 * @param share_mode sharing type - will determine whether the below arguments are necessary
 * @param type the sel4utils_reservation_type_t of the VMR
 * @param start  OPTIONAL start address of the VMR (in the current ADS)
 * @param dest_start OPTIONAL start address of the VMR (in the destination ADS)
 * @param region_pages OPTIONAL number of pages in the VMR
 * @param page_bits OPTIONAL size of an individual page
 * @param mo OPTIONAL an MO to map to the VMR
 */
void sel4gpi_add_vmr_config(ads_config_t *cfg,
                            gpi_share_degree_t share_mode,
                            sel4utils_reservation_type_t type,
                            void *start,
                            void *dest_start,
                            uint64_t region_pages,
                            size_t page_bits,
                            mo_client_context_t *mo);

/**
 * @brief creates a new stack with num_pages in the given ADS, it will NOT be mapped to the current one.
 *        NOTE: no guard page is created
 * @param ads the ADS in which to create the stack
 * @param n_pages number of pages for the stack
 * @param[out] ret_mo OPTIONAL: returns the MO backing the stack
 * @return the top of the stack in the given ADS
 */
void *sel4gpi_new_sized_stack(ads_client_context_t *ads, size_t n_pages, mo_client_context_t *ret_mo);

/**
 * @brief Convenience function for configuring a new resource type to share with a PD
 * All resources of the given type will be shared with the PD created by this config
 * Will malloc a new config list node, which gets freed during sel4gpi_config_destroy
 *
 * @param cfg PD config to populate
 * @param resource_type the resource type to share
 */
void sel4gpi_add_res_type_config(pd_config_t *cfg, gpi_cap_t resource_type);
