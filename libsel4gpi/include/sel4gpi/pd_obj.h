
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <sel4utils/process.h>
#include <sel4utils/process_config.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/badge_usage.h>
#include <sel4gpi/cap_tracking.h>
#include <sel4gpi/ads_clientapi.h>
#include <utils/uthash.h>

#define TEST_NAME_MAX (64 - 4 * sizeof(seL4_Word))
#define MAX_SYS_OSM_CAPS 5000
#define MAX_MO_CHILD 10

#define MAX_PD_NAME 64
#define MAX_PD_OSM_CAPS 512
#define MAX_PD_OSM_RDE (GPICAP_TYPE_MAX + (MAX_NS_PER_RDE * (GPICAP_TYPE_MAX - 1)))
#define MAX_NS_PER_RDE 8

#define MAX_PD_INIT_CAPS 8

#define PD_ALLOCATOR_STATIC_POOL_SIZE ((1 << seL4_PageBits) * 20)

// (XXX) This is not yet used anywher.
typedef struct pd_name
{
    char top[MAX_PD_NAME];
    char mid[MAX_PD_NAME];
    char end[MAX_PD_NAME];
} pd_name_t;

typedef union rde_type
{
    // We have talked about tracking RDE for speicific resdources,
    // For instance, say one FILE cap came from one PD, and another from another.
    // Or if a resources was handed down from another PD, should that PD be the RDE,
    // or the PD that that created it should be RDE.
    // seL4_Word slot_in_PD;
    gpi_cap_t type;
} rde_type_t;

typedef struct osmosis_rde
{
    // The slot of the RDE cap as per seL4
    seL4_Word slot_in_RT;
    seL4_Word slot_in_PD;

    /*
        I think that type+pd_obj_id should be all we need
        to find out when OSM resources are shared.
        But let's keep track of slot_in* (above) for now.
    */

    /* OSmosis generated resource manager ID for RDE */
    uint32_t pd_obj_id;
    uint32_t manager_id;
    uint32_t ns_id;

    /* Info about what the RDE is for ?*/
    rde_type_t type;

    // This is indexed like so:
    //   0:GPICAP_TYPE_MAX - 1 = RDEs in their default namespaces, e.g. ns_id = 0
    //   GPICAP_TYPE_MAX: GPICAP_TYPE_MAX + (MAX_NS_PER_RDE - 1) = namespaces for the first RDE type
    //   ... continues in the same order as labeled in the gpi_cap_t enum
} osmosis_rde_t;

typedef struct osmosis_pd_cap
{
    // The slot of the cap as per seL4
    // do not rely on these, as this info is sometimes difficult to find
    seL4_Word slot_in_PD_Debug;
    seL4_Word slot_in_RT_Debug;
    seL4_Word slot_in_ServerPD_Debug; // For instance in case of file.

    /*
        I think that type+res_id should be all we need
        to find out when OSM resources are shared.
        But let's keep track of slot_in* (above) for now.
    */
    uint32_t res_id; // key to uthash

    /*
        Type is PD/MO/CPU/ADS then look locally, else
        Copy the cap from PD to RT and then calls RR on it.
    */
    gpi_cap_t type;
    UT_hash_handle hh;
} osmosis_pd_cap_t;

/**
 * The data given to initialize a new Osmosis PD
 */
typedef struct _osm_pd_init_data
{
    // PD's own core resources
    seL4_CPtr pd_cap;
    seL4_CPtr ads_cap;
    seL4_CPtr cpu_cap;

    // Resource directory
    // ADS ID of the PD's current binded ADS
    uint32_t binded_ads_ns_id;
    osmosis_rde_t rde[MAX_PD_OSM_RDE];
    uint64_t rde_count;
} osm_pd_init_data_t;

typedef struct _pd
{
    // seL4_CPtr cspace_root;
    seL4_CPtr fault_endpoint_in_pd;

    /* One of these we will keep */
    uint32_t pd_obj_id;

    sel4utils_process_t proc;

    // CPU_CAP
    simple_t *simple;
    vka_t *vka;
    vspace_t *vspace;

    /* page directory of the test process */
    seL4_CPtr page_directory_in_pd;
    /* root cnode of the test process */
    seL4_CPtr root_cnode_in_pd;
    /* tcb of the test process */
    seL4_CPtr tcb_in_pd;
    /* the domain cap */
    seL4_CPtr domain_in_pd;
    /* asid pool cap for the test process to use when creating new processes */
    seL4_CPtr asid_pool_in_pd;
    seL4_CPtr asid_ctrl_in_pd;
#ifdef CONFIG_IOMMU
    seL4_CPtr io_space;
#endif /* CONFIG_IOMMU */
#ifdef CONFIG_TK1_SMMU
    seL4_SlotRegion io_space_caps;
#endif

    /* copied (by sel4test-driver) notification cap that tests can wait
     * on when requesting a time service from sel4test-driver (e.g. sleep),
     * and expecting a signal and/or notification.
     */
    seL4_CPtr timer_ntfn_in_pd;

    /* size of the test processes cspace */
    seL4_Word cspace_size_bits;
    /* range of free slots in the cspace */
    seL4_SlotRegion free_slots;

    /* range of untyped memory in the cspace */
    seL4_SlotRegion untypeds;
    /* size of untyped that each untyped cap corresponds to
     * (size of the cap at untypeds.start is untyped_size_bits_lits[0]) */
    uint8_t untyped_size_bits_list[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];
    /* name of the test to run */
    char name[TEST_NAME_MAX];
    /* priority the test process is running at */
    int priority;

    /* sched control cap */
    seL4_CPtr sched_ctrl_in_pd;

    /* device frame cap */
    seL4_CPtr device_frame_cap;

    /* List of elf regions in the test process image, this
     * is provided so the test process can launch copies of itself.
     *
     * Note: copies should not rely on state from the current process
     * or the image. Only use copies to run code functions, pass all
     * required state as arguments. */
    sel4utils_elf_region_t elf_regions[20];

    /* the number of elf regions */
    int num_elf_regions;

    /* the number of pages in the stack */
    int stack_pages;

    /* address of the stack */
    void *stack;

    /* freq of the tsc (for x86) */
    uint32_t tsc_freq;

    /* number of available cores */
    seL4_Word cores;

    /* allocator for the pd's cspace */
    char allocator_mem_pool[PD_ALLOCATOR_STATIC_POOL_SIZE];
    vka_t pd_vka;

    /*
        There are the resources and RDE which we are explicitly tracking for a PD.
        They should be updated when the PD get's a new cap via pd_send_cap
        or a new RDE is added at PD creation time.

        (XXX): This is not yet fully the case yet.

        (XXX) Convert both of there to linked lists
    */

    // PD start state
    int pd_started; // whether or not the pd has been started

    // PD's accessible resources
    osmosis_pd_cap_t *has_access_to;
    uint64_t has_access_to_count;

    // Init data is mapped to PD and includes RDE and ADS/PD caps
    mo_client_context_t init_data_mo;
    seL4_CPtr init_data_frame;
    uint64_t init_data_mo_id;
    osm_pd_init_data_t *init_data;       // RT vaddr of the init data
    osm_pd_init_data_t *init_data_in_PD; // PD's vaddr of the init data

    // Special caps to send to all PDs
    seL4_CPtr pd_cap_in_RT;
    seL4_CPtr ads_cap_in_RT;
    seL4_CPtr cpu_cap_in_RT;
    uint32_t ads_obj_id;

    /**
     * =========================================================================
     *       Field which should ideadlly not be here.
     * =========================================================================
     */
    sel4utils_process_config_t config;

} pd_t;

/*
What caps data
Type:
Permissions:
EP if applicable

*/

int pd_new(pd_t *pd,
           vka_t *server_vka,
           vspace_t *server_vspace);

int pd_load_image(pd_t *pd,
                  vka_t *vka,
                  simple_t *simple,
                  const char *image_path,
                  vspace_t *server_vspace,
                  vspace_t *target_vspace,
                  vka_object_t *target_vspace_root_page_dir);

int pd_dump(pd_t *pd);

int pd_send_cap(pd_t *pd,
                seL4_CPtr cap,
                seL4_Word badge,
                seL4_Word *slot);

int pd_start(pd_t *pd,
             vka_t *vka,
             seL4_CPtr pd_endpoint_in_root,
             vspace_t *vspace,
             int argc,
             seL4_Word *args);

int pd_next_slot(pd_t *pd,
                 seL4_CPtr *next_free_slot);

int pd_free_slot(pd_t *pd,
                 seL4_CPtr slot);

/**
 * Allocates an endpoint using the gpi server's vka, and copies to the pd cspace
 *
 * @param pd The pd to allocate an endpoint for
 * @param server_vka VKA of the gpi server
 * @param ret_ep slot of the allocated ep in the PD's cspace
 */
int pd_alloc_ep(pd_t *pd,
                vka_t *server_vka,
                seL4_CPtr *ret_ep);

/**
 * Mints a source path into the PD's cspace
 *
 * @param pd The destination PD
 * @param src Path to the source cap
 * @param badge Badge to apply
 * @param ret Returns the destination slot in the PD
 */
int pd_mint(pd_t *pd,
            cspacepath_t *src,
            seL4_Word badge,
            seL4_CPtr *ret);

/**
 * Mints an endpoint in the PD's cspace and attaches the badge
 *
 * @param pd The pd to allocate an endpoint for
 * @param src_ep raw endpoint to badge
 * @param badge badge to apply
 * @param ret_ep slot of the badged ep in the PD's cspace
 */
int pd_badge_ep(pd_t *pd,
                seL4_CPtr src_ep,
                seL4_Word badge,
                seL4_CPtr *ret_ep);

/**
 * Bootstraps a VKA allocator for the PD's cspace
 * Requires an existing 1-level cspace
 *
 * @param root The root cnode for a 1-level cspace
 * @param start_slot First free slot in the cspace
 * @param end_slot Last free slot in the cspace
 * @param size_bits Size bits of the entire cspace
 * @param guard_bits Number of guard bits used for this cspace
 */
int pd_bootstrap_allocator(pd_t *pd,
                           seL4_CPtr root,
                           size_t start_slot,
                           size_t end_slot,
                           size_t size_bits,
                           size_t guard_bits);

void print_pd_osm_cap_info(osmosis_pd_cap_t *o);
void print_pd_osm_rde_info(osmosis_rde_t *o);

osmosis_pd_cap_t *pd_add_resource(pd_t *pd,
                                  gpi_cap_t type,
                                  uint32_t res_id,
                                  seL4_CPtr slot_in_RT,
                                  seL4_CPtr slot_in_PD,
                                  seL4_CPtr slot_in_serverPD);

/**
 * @brief Add an RDE to a PD
 *
 * Note: This must be called after the PD is loaded, and before it is started
 *
 * @param pd The target PD to add an RDE to
 * @param type the type of the RDE
 * @param manager_id the resource manager ID of this RDE
 * @param server_ep the raw endpoint of the resource manager
 */
int pd_add_rde(pd_t *pd,
               rde_type_t type,
               uint32_t manager_id,
               uint32_t ns_id,
               seL4_CPtr server_ep);

/**
 * @brief Send a cap to a PD's cspace
 *
 * @param to_pd destination PD
 * @param cap the cap to send
 * @param slot the slot in destination cspace
 */
int copy_cap_to_pd(pd_t *to_pd,
                   seL4_CPtr cap,
                   seL4_Word *slot);
