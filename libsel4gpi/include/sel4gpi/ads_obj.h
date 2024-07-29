
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
#include <sel4gpi/resource_registry.h>
#include <sel4gpi/pd_creation.h>

typedef struct _pd pd_t;
typedef struct _mo mo_t;
typedef struct _cpu cpu_t;

/**
 * Maps a shorter (portable) attach node ID to a vaddr
 */
typedef struct _attach_node_map
{
    resource_registry_node_t gen;
    void *vaddr; ///< Key for the attach registry
} attach_node_map_t;

/**
 * Represents an reservation in the ADS (essentially a VMR resource)
 * Optinoally, also an attachment of an MO
 *
 * Each attach of the same MO requires a copy of the frame capabilities
 * */
typedef struct _attach_node
{
    resource_registry_node_t gen;

    void *vaddr;                       ///< Attach vaddr, key for the UTHash
    attach_node_map_t *map_entry;      ///< the attach node map entry for this node
    reservation_t res;                 ///< Reservation in the vspace
    sel4utils_reservation_type_t type; ///< Reservation type
    uint32_t n_pages;                  ///< Number of pages
    size_t page_bits;                  ///< size bits of an individual page
    bool cacheable;                    ///< True if the pages are cacheable
    seL4_CapRights_t rights;           ///< Rights to the pages

    // (XXX) Arya: Assumes we only need to attach one MO to a reservation
    bool mo_attached;      ///< True if an MO is attached (next fields are valid only if true)
    size_t mo_offset;      ///< Offset where the MO is attached in the reservation
    gpi_obj_id_t mo_id;    ///< ID of the MO attached
    seL4_CPtr *frame_caps; ///< Array of frame caps copied for this attach
    uint32_t n_frames;     ///< Number of frame caps in the array
} attach_node_t;

typedef struct _ads
{
    gpi_obj_id_t id;

    vspace_t *vspace;
    vka_object_t *root_page_dir;
    sel4utils_process_t *process_for_cookies;

    resource_registry_t attach_registry;
    resource_registry_t attach_id_to_vaddr_map;
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
 * @brief Initialize an ads object
 * This is exposed just to initialize a forged ADS object
 *
 * @param ads the ADS to initialize
 * @return int 0 on success, 1 on failure.
 */
int ads_initialize(ads_t *ads);

/**
 * Reserve a region of the ADS
 *
 * @param ads ads object
 * @param vaddr OPTIONAL: virtual address to reserve
 * @param num_pages num of pages to reserve
 * @param size_bits size of the pages
 * @param vmr_type the type of VMR, e.g. heap, stack, IPC buffer, etc.
 * @param cacheable if true, the pages are cacheable
 * @param rights rights for the pages
 * @param ret_node returns the created reservation node
 * @return int 0 on success, 1 on failure.
 */
int ads_reserve(ads_t *ads,
                void *vaddr,
                uint32_t num_pages,
                size_t size_bits,
                sel4utils_reservation_type_t vmr_type,
                bool cacheable,
                seL4_CapRights_t rights,
                attach_node_t **ret_node);

/**
 * Get an attach node from the ADS by ID
 *
 * @param ads ads object
 * @param res_id the ID of the reservation to find
 * @return the corresponding attach node, or NULL if not found
 */
attach_node_t *ads_get_res_by_id(ads_t *ads, gpi_obj_id_t res_id);

/**
 * Get an attach node from the ADS by vaddr
 *
 * @param ads ads object
 * @param vaddr the vaddr of the reservation to find
 * @return the corresponding attach node, or NULL if not found
 */
attach_node_t *ads_get_res_by_vaddr(ads_t *ads, void *vaddr);

/**
 * @brief finds the reservations for a VMR by the type (Multiple reservations of the type may exist)
 *
 * @param src_ads ADS to find the reservation in
 * @param vmr_type the type of VMR to look for
 * @return returns a list of found attach nodes
 */
linked_list_t *ads_get_res_by_type(ads_t *src_ads, sel4utils_reservation_type_t vmr_type);

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
 * @param cacheable if true, the pages are cacheable
 * @param rights rights for the pages
 * @param ret_vaddr returns vaddr attached at
 * @return int 0 on success, 1 on failure.
 */
int ads_attach(ads_t *ads,
               vka_t *vka,
               void *vaddr,
               mo_t *mo,
               bool cacheable,
               seL4_CapRights_t rights,
               void **ret_vaddr,
               sel4utils_reservation_type_t vmr_type);

/**
 * Use to forge an ADS attach from a vspace attach
 * Will assume the page size is the same as the MO frames
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
 * @brief Shallow copies a VMR from src_ads to dst_ads.
 * If config only specifies a VMR type that is neither SEL4UTILS_RES_TYPE_GENERIC nor SEL4UTILS_RES_TYPE_SHARED_FRAMES,
 * will search for the VMR reservation corresponding to the given type
 *
 * @param loader the current vspace
 * @param vka vka object for cspace and page table allocations
 * @param src_ads source ADS to copy the VMR from
 * @param dst_ads target ADS to copy into
 * @param cfg the configuration describing the VMR to copy
 * @return int
 */
int ads_shallow_copy(vspace_t *loader, vka_t *vka, ads_t *src_ads, ads_t *dst_ads, vmr_config_t *cfg);

/**
 * @param ads ads object to dump the RR for
 * @param ms pointer to model state
 * @param pd_node the existing node for pd that is being dumped
 * @return gpi_model_node_t * the model state node for the resource space
 */
gpi_model_node_t *ads_dump_rr(ads_t *ads, model_state_t *ms, gpi_model_node_t *pd_node);

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
 * @brief loads an ELF with image_name into the given ads
 *
 * @param loadee the target ADS
 * @param loader the root task ADS
 * @param pd the PD for which the elf is being loaded
 * @param image_name name of the ELF image to load
 * @param ret_entry_point the entry point found after loading the ELF
 * @return 0 on success, 1 on failure
 */
int ads_load_elf(ads_t *loadee,
                 ads_t *loader,
                 pd_t *pd,
                 const char *image_name,
                 void **ret_entry_point);

/**
 * @brief writes ELF information, Aux vectors, and user arguments onto the
 * stack, in preparation for a sel4runtime setup
 *
 * This is slightly modified version of the sel4utils_process_spawn_v function
 *
 * @param pd target PD to write arguments for, it's expected that an ELF has already been loaded into its ADS,
 *          and the relevant ELF data fields inside pd_t are filled out
 * @param loadee_vspace vspace handle of the ADS where the stack to write in resides
 * @param ipc_buf_addr OPTIONAL address to the IPC buffer
 * @param stack_top the top of the stack in loadee_vspace
 * @param argc the number of arguments
 * @param argv the arguments
 * @param ret_init_stack the position of the initial stack pointer after setup
 * @return 0 on success, 1 on failure
 */
int ads_write_stack_runtime(pd_t *pd,
                            vspace_t *loadee_vspace,
                            void *ipc_buf_addr,
                            void *stack_top,
                            int argc,
                            char *argv[],
                            void **ret_init_stack);

/**
 * @brief write string arguments onto the stack
 *
 * @param target_ads ADS which the stack is mapped in
 * @param argc number of arguments
 * @param argv string args
 * @param stack_top the top of the stack in target_ads
 * @param ret_init_stack the aligned stack pointer after arguments have been written
 * @return int 0 on success, 1 on failure
 */
int ads_write_stack_args(ads_t *target_ads,
                         int argc,
                         char *argv[],
                         void *stack_top,
                         void **ret_init_stack);
