
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
#include <sel4gpi/cpu_obj.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/gpi_client.h>
#include <sel4gpi/pd_creation.h>

// Used in a map from attach node ID to vaddr
typedef struct _attach_node_map
{
    resource_server_registry_node_t gen;
    void *vaddr; // Key for the attach registry
} attach_node_map_t;

/**
 * Represents an reservation in the ADS (essentially a VMR resource)
 * Optinoally, also an attachment of an MO
 *
 * Each attach of the same MO requires a copy
 * of the frame capabilities
 * */
typedef struct _attach_node
{
    resource_server_registry_node_t gen;

    void *vaddr;                  // Key for the UTHash
    attach_node_map_t *map_entry; // the attach node map entry for this node
    reservation_t res;
    sel4utils_reservation_type_t type;
    uint32_t n_pages;

    // Only if an MO is attached
    // (XXX) Arya: Assumes we only need to attach one MO to a reservation
    bool mo_attached;
    size_t mo_offset;
    seL4_Word mo_id;
    seL4_CPtr *frame_caps;
    uint32_t n_frames;
} attach_node_t;

typedef struct _ads
{
    uint32_t id;

    vspace_t *vspace;
    vka_object_t *root_page_dir;
    sel4utils_process_t *process_for_cookies;

    resource_server_registry_t attach_registry;
    resource_server_registry_t attach_id_to_vaddr_map;
} ads_t;

/**
 * @brief Create a new ads object.
 *
 * @param ads pointer to ads structure
 * @param vka vka object to allocate cspace slots and PT from
 * @param loader vspace of the function running this
 * @param arg0 unused
 * @return int 0 on success, 1 on failure.
 */
int ads_new(ads_t *ads,
            vka_t *vka,
            vspace_t *loader,
            void *arg0);

/**
 * Reserve a region of the ADS
 *
 * @param ads ads object
 * @param vaddr virtual address to reserve
 * @param num_pages num of pages to reserve
 * @param size_bits size of the pages
 * @param vmr_type the type of VMR, e.g. heap, stack, IPC buffer, etc.
 * @param ret_node returns the created reservation node
 * @return int 0 on success, 1 on failure.
 */
int ads_reserve(ads_t *ads,
                void *vaddr,
                uint32_t num_pages,
                size_t size_bits,
                sel4utils_reservation_type_t vmr_type,
                attach_node_t **ret_node);

/**
 * Get an attach node from the ADS by ID
 *
 * @param ads ads object
 * @param res_id the ID of the reservation to find
 * @return the corresponding attach node, or NULL if not found
 */
attach_node_t *ads_get_res_by_id(ads_t *ads, uint64_t res_id);

/**
 * Get an attach node from the ADS by vaddr
 *
 * @param ads ads object
 * @param vaddr the vaddr of the reservation to find
 * @return the corresponding attach node, or NULL if not found
 */
attach_node_t *ads_get_res_by_vaddr(ads_t *ads, void *vaddr);

/**
 * Attach an MO to an existing ADS reservation
 *
 * @param ads ads object
 * @param vka vka object to allocate cspace slots and PT from
 * @param reservation the existing reservation
 * @param offset offset into the reservation to attach the MO at
 * @param mo the MO to attach
 * @return int 0 on success, 1 on failure.
 */
int ads_attach_to_res(ads_t *ads,
                      vka_t *vka,
                      attach_node_t *reservation,
                      size_t offset,
                      mo_t *mo);

/**
 * @brief Attach an MO at a given address to the ADS.
 * Makes a corresponding VMR reservation at the same time.
 *
 * @param ads ads object
 * @param vka vka object to allocate cspace slots and PT from
 * @param vaddr virtual address to attach the frame to
 * @param mo MO to attach
 * @param ret_vaddr returns vaddr attached at
 * @return int 0 on success, 1 on failure.
 */
int ads_attach(ads_t *ads,
               vka_t *vka,
               void *vaddr,
               mo_t *mo,
               void **ret_vaddr,
               sel4utils_reservation_type_t vmr_type);

/**
 * Use to forge an ADS attach from a vspace attach
 * (XXX) Arya: to be deprecated eventually
 *
 * @param ads ads object
 * @param res the sel4utils_res object
 * @param mo the forged MO
 */
int ads_forge_attach(ads_t *ads, sel4utils_res_t *res, mo_t *mo);

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
 * @param vka vka object to allocate cspace slots and PT from
 * @param src_ads ads object to clone
 * @param dst_ads target to copy into
 * @param omit_vaddr start vaddr of the segment to omit
 * @param pd_osm_data vaddr of a PDs osm data (e.g. RD table) - we need to shallow copy this
 * @param shallow_copy if true, only copy the page table entries, do not copy the frames
 * @return int
 */
int ads_copy(vspace_t *loader, vka_t *vka, ads_t *src_ads, ads_t *dst_ads, vmr_config_t *cfg);

/**
 * @param ads ads object to dump the RR for
 * @param ms pointer to model state
 * @param pd_node the existing node for pd that is being dumped
 * @return void
 */
void ads_dump_rr(ads_t *ads, model_state_t *ms, gpi_model_node_t *pd_node);

/**
 * Destroys an ADS, including all metadata and internal tracking
 * Does not destroy any MOs that are attached to the ADS
 *
 * This does not remove the ADS from the ADS component registry
 * This function should only be called by the ADS component
 *
 * @param ads the ads object
 */
void ads_destroy(ads_t *ads);

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
 * @param image_name name of the ELF image to load
 * @return 0 on success, 1 on failure
 */
int ads_load_elf(vspace_t *loadee_vspace, sel4utils_process_t *proc, const char *image_name, void **ret_entry_point);

/**
 * @brief slightly modified version of the sel4utils process spawn function
 * sets up the stack with arguments for setting up the C runtime
 *
 * @param process the process struct holding info after an ELF has been loaded
 * @param osm_init_data initial OSmosis data for the PD
 * @param vka the loader's VKA
 * @param vspace the loader's vspace
 * @param argc the number of arguments
 * @param argv the arguments
 * @param ret_init_stack the position of the initial stack pointer after setup
 * @return 0 on success, 1 on failure
 */
int ads_write_arguments(sel4utils_process_t *process,
                        void *osm_init_data,
                        vka_t *vka,
                        vspace_t *vspace,
                        int argc,
                        char *argv[],
                        void **ret_init_stack);
