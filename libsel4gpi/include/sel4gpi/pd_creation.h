#pragma once

#include <stdint.h>
#include <sel4/types.h>
#include <utils/ansi_color.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/cpu_clientapi.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/resource_space_clientapi.h>

#define DEFAULT_STACK_PAGES 16
#define DEFAULT_HEAP_PAGES 100
#define MAX_CFGS 100

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

// holds the components for a runnable entity
typedef struct _sel4gpi_runnable
{
    pd_client_context_t pd;
    ads_client_context_t ads;
    cpu_client_context_t cpu;
} sel4gpi_runnable_t;

// Configuration types for creating new PDs: for a given resource, defines the level of sharing between a given source PD and the new PD

// describes sharing between 2 PDs
typedef enum _gpi_share_degree
{
    GPI_SHARED = 1, // this resource is directly shared with the other PD, e.g. virt pages that map to the same phys page
    GPI_COPY,       // this resource is copied into the other PD, e.g. virt pages with separate phys pages with contents copied
    GPI_DISJOINT,   // this resource exists in the other PD, but has no relation with the source PD
    GPI_OMIT        // this resource will not exist in the other PD
} gpi_share_degree_t;

// configuration of a particular VMR, do not use for the stack and ELF regions
typedef struct _vmr_config
{
    gpi_share_degree_t share_mode;
    sel4utils_reservation_type_t type;
    void *start;           // vaddr to start of the VMR
    uint64_t region_pages; // number of pages in this VMR
} vmr_config_t;

// Configuration of an entire ADS
typedef struct _ads_config
{
    bool same_ads;                 // whether this config is for the same ADS as the current one
    ads_client_context_t *src_ads; // the source ADS to generate the new ADS, only used if same_ads == false
    const char *image_name;        // only used if code_shared == GPI_DISJOINT
    void *entry_point;             // if specified, will take precedence over any automatically found ones

    /* special ADS regions */
    /*  if we're in the same ADS, configuring any of these as GPI_SHARED has no effect */
    gpi_share_degree_t code_shared;
    gpi_share_degree_t stack_shared;
    gpi_share_degree_t ipc_buf_shared;
    size_t stack_pages;

    /* list of vaddrs to non-contiguous VMRs to configure */
    /* if we're in the same ADS, configuring any of these as GPI_SHARED has no effect */
    /* the heap should be specified here */
    size_t n_vmr_cfg;
    vmr_config_t vmr_cfgs[MAX_CFGS];
} ads_config_t;

// defines what RDE type, and which resource space to share with the new PD
typedef struct _rde_config
{
    gpi_cap_t type;
    uint32_t space_id;
} rde_config_t;

// For creating new PDs: defines the level of sharing between a given source PD and the new PD
typedef struct _pd_config
{
    seL4_CPtr fault_ep; // supply a fault-endpoint for the PD, if NULL, will create a new one
    ads_config_t ads_cfg;
    rde_config_t rde_cfg[MAX_CFGS];
    size_t n_rde_cfg;
    // ongoing: add configs for other resources here as needed
} pd_config_t;

/**
 * @brief creates a new PD, and generates a configuration that can start a process
 * by default, gives the new process the MO and RESSPC RDE
 *
 * @param image_name ELF image for the process
 * @param stack_pages size of stack, in pages
 * @param heap_pages size of heap, in pages
 * @param ret_pd returns the newly created PD context
 * @return pd_config_t* returns the PD configuration, NULL on failure. Caller is responsbile for freeing the config.
 */
pd_config_t *sel4gpi_configure_process(const char *image_name, int stack_pages, int heap_pages, pd_client_context_t *ret_pd);

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
 * @return pd_config_t* returns a filled in config struct, caller is responsbile for freeing
 */
pd_config_t *sel4gpi_generate_proc_config(const char *image_name, size_t stack_pages, size_t heap_pages);

/**
 * @brief generates a PD configuration that describes a thread
 *
 * @param thread_fn the thread's entry function
 * @param fault_ep the fault endpoint for the thread (OPTIONAL, if not specified, a new one will be allocated)
 * @return pd_config_t*
 */
pd_config_t *sel4gpi_generate_thread_config(void *thread_fn, seL4_CPtr fault_ep);
