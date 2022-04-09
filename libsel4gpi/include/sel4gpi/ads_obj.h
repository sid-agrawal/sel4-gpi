
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

typedef struct _ads {
    vspace_t vspace;
}ads_t;

/**
 * @brief Attach a frame at a given address to the ads.
 * 
 * @param ads ads object
 * @param vka vka object to allocate cspace slots and PT from
 * @param vaddr virtual address to attach the frame to
 * @param size size of the frame
 * @return int 0 on success, -1 on failure.
 */
int ads_attach(ads_t *ads, vka_t *vka, void* vaddr, size_t size);


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