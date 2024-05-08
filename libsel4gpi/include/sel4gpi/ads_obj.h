
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>
#include <sel4utils/vspace_internal.h>
#include <sel4utils/process.h>
#include <sel4gpi/model_exporting.h>
#include <sel4gpi/mo_obj.h>

/**
 * Represents an attachment of a memory object
 * to an ADS
 *
 * Each attach of the same MO requires a copy
 * of the frame capabilities
 * */
typedef struct _attach_node
{
    seL4_Word mo_id;
    sel4utils_reservation_type_t type;
    void *vaddr;
    uint32_t n_pages;
    seL4_CPtr *frame_caps;
    struct _attach_node *next;
} attach_node_t;

typedef struct _ads
{
    vspace_t *vspace;
    vka_object_t *root_page_dir;
    sel4utils_process_t *process_for_cookies;
    uint32_t ads_obj_id;
    attach_node_t *attach_nodes;
} ads_t;

/**
 * @brief Create a new ads object.
 *
 * @param loader vspace of the function running this
 * @param vka vka object to allocate cspace slots and PT from
 * @param ret_ads return ads object
 * @return int 0 on success, -1 on failure.
 */
int ads_new(vspace_t *loader,
            vka_t *vka,
            ads_t *ret_ads);

/**
 * @brief Attach a frame at a given address to the ads.
 *
 * @param ads ads object
 * @param vaddr virtual address to attach the frame to
 * @param num_pages num of pages to attach
 * @param size_bits size of the pages
 * @param frame_caps caps to the frames to attach
 * @param mo_id (optional) ID of the MO these frames are from
 * @param ret_vaddr returns vaddr attached at
 * @param vmr_type the type of VMR, e.g. heap, stack, IPC buffer, etc.
 * @return int 0 on success, -1 on failure.
 */
int ads_attach(ads_t *ads,
               void *vaddr,
               uint32_t num_pages,
               size_t size_bits,
               seL4_CPtr *frame_caps,
               uint32_t mo_id,
               void **ret_vaddr,
               sel4utils_reservation_type_t vmr_type);

/**
 * @brief Attach an MO at a given address to the ads.
 *
 * @param ads ads object
 * @param vka vka object to allocate cspace slots and PT from
 * @param vaddr virtual address to attach the frame to
 * @param mo MO to attach
 * @param ret_vaddr returns vaddr attached at
 * @return int 0 on success, -1 on failure.
 */
int ads_attach_mo(ads_t *ads,
                  vka_t *vka,
                  void *vaddr,
                  mo_t *mo,
                  void **ret_vaddr,
                  sel4utils_reservation_type_t vmr_type);

/**
 * @brief Remove a region from the ADS
 * Requires that a region was attached using ads_attach at the given vaddr
 *
 * @param ads ads object
 * @param vka vka object to allocate cspace slots and PT from
 * @param vaddr virtual address at the beginning of the region to remove
 * @return int 0 on success, -1 on failure.
 */
int ads_rm(ads_t *ads, vka_t *vka, void *vaddr);

/**
 * @brief
 *
 * @param ads ads object
 * @param vka vka object to allocate cspace slots and PT from
 * @param cpu_cap use this as the ads for the give TCB
 * @return int
 */
int ads_bind(ads_t *ads, vka_t *vka, seL4_CPtr *cpu_cap);

/**
 * @brief
 *
 * @param loader vspace of the function running this
 * @param ads ads object to clone
 * @param vka vka object to allocate cspace slots and PT from
 * @param omit_vaddr start vaddr of the segment to omit
 * @param pd_osm_data vaddr of a PDs osm data (e.g. RD table) - we need to shallow copy this
 * @param shallow_copy if true, only copy the page table entries, do not copy the frames
 * @param ret_ads return ads of the cloned ads
 * @return int
 */
int ads_shallow_copy(vspace_t *loader, ads_t *ads, vka_t *vka, void *omit_vaddr,
                     void *pd_osm_data, bool shallow_copy, ads_t *ret_ads);

/**
 * @param ads ads object to dump the RR for
 * @param ms pointer to model state
 * @param pd_node the existing node for pd that is being dumped
 * @return void
 */
void ads_dump_rr(ads_t *ads, model_state_t *ms, gpi_model_node_t *pd_node);

static seL4_CPtr get_asid_pool(seL4_CPtr asid_pool)
{
    if (asid_pool == 0)
    {
        ZF_LOGW("This method will fail if run in a thread that is not in the root server cspace\n");
        asid_pool = seL4_CapInitThreadASIDPool;
    }

    return asid_pool;
}

static seL4_CPtr assign_asid_pool(seL4_CPtr asid_pool, seL4_CPtr pd)
{
    int error = seL4_ARCH_ASIDPool_Assign(get_asid_pool(asid_pool), pd);
    if (error)
    {
        ZF_LOGE("Failed to assign asid pool\n");
    }

    return error;
}

/* ======================================= CONVENIENCE FUNCTIONS (NOT PART OF FRAMEWORK) ================================================= */

/**
 * @brief loads an ELF with image_name into the given vspace
 *
 * @param loadee_vspace
 * @param proc the process struct that belongs to the PD obj
 * @param image_name
 * @return int
 */
int ads_load_elf(vspace_t *loadee_vspace, sel4utils_process_t *proc, char *image_name);

/**
 * @brief slightly modified version of the sel4utils process spawn function
 * sets up the stack, but doesn't actually start the process
 *
 * @param process
 * @param osm_init_data
 * @param vka
 * @param vspace
 * @param argc
 * @param argv
 * @return int
 */
int ads_proc_setup(sel4utils_process_t *process,
                   void *osm_init_data,
                   vka_t *vka,
                   vspace_t *vspace,
                   int argc,
                   char *argv[]);
