
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

typedef struct _ads {
    vspace_t *vspace;
    uint32_t ads_obj_id;
}ads_t;

/**
 * @brief Attach a frame at a given address to the ads.
 * 
 * @param ads ads object
 * @param vka vka object to allocate cspace slots and PT from
 * @param vaddr virtual address to attach the frame to
 * @param size size of the frame
 * @param frame_cap cap to the frame to attach
 * @return int 0 on success, -1 on failure.
 */
int ads_attach(ads_t *ads, vka_t *vka, void* vaddr, size_t size, seL4_CPtr frame_cap, sel4utils_process_t *process_cookie);


/**
 * @brief Remove a frame from the ads.
 * 
 * @param ads ads object
 * @param vka vka object to allocate cspace slots and PT from
 * @param vaddr virtual address to remove the frame from
 * @param size size of the frame
 * @return int 0 on success, -1 on failure.
 */
int ads_rm(ads_t *ads, vka_t *vka, void* vaddr, size_t size);


/**
 * @brief 
 * 
 * @param ads ads object
 * @param vka vka object to allocate cspace slots and PT from
 * @param cpu_cap use this as the ads for the give TCB
 * @return int 
 */
int ads_bind(ads_t *ads, vka_t *vka, seL4_CPtr* cpu_cap);

/**
 * @brief
 *
 * @param loader vspace of the function running this
 * @param ads ads object to clone
 * @param vka vka object to allocate cspace slots and PT from
 * @param omit_vaddr start vaddr of the segment to omit
 * @param ret_ads return ads of the cloned ads
 * @return int
 */
int ads_clone(vspace_t *loader, ads_t *ads, vka_t *vka, void* omit_vaddr, ads_t *ret_ads);


static seL4_CPtr get_asid_pool(seL4_CPtr asid_pool)
{
    if (asid_pool == 0) {
        ZF_LOGW("This method will fail if run in a thread that is not in the root server cspace\n");
        asid_pool = seL4_CapInitThreadASIDPool;
    }

    return asid_pool;
}

static seL4_CPtr assign_asid_pool(seL4_CPtr asid_pool, seL4_CPtr pd)
{
    int error = seL4_ARCH_ASIDPool_Assign(get_asid_pool(asid_pool), pd);
    if (error) {
        ZF_LOGE("Failed to assign asid pool\n");
    }

    return error;
}
