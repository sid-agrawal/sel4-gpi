
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

#define MO_PAGE_BITS seL4_PageBits

typedef struct _mo
{
    uint32_t id;

    seL4_CPtr *frame_caps_in_root_task;
    vka_object_t *vka_objects;
    uintptr_t *frame_paddrs;
    uint32_t num_pages;
} mo_t;

/**
 * @brief Create a new MO object
 *
 * @param mo mo object
 * @param vka vka object to allocate frames from
 * @param vspace unused
 * @param num_pages number of pages to allocate
 * @return int 0 on success, 1 on failure.
 */
int mo_new(mo_t *mo,
           vka_t *vka,
           vspace_t *vspace,
           int num_pages);

/**
 * @param mo mo object to dump the RR for
 * @param ms pointer to model state
 * @param pd_node the existing node for pd that is being dumped
 * @return void
 */
void mo_dump_rr(mo_t *mo, model_state_t *ms, gpi_model_node_t *pd_node);

/**
 * Destroys an MO, including all metadata and the underlying frames
 *
 * This does not remove the MO from the MO component registry
 * This function should only be called by the MO component
 *
 * @param mo the mo object
 * @param server_vka the vka to free frames from
 */
void mo_destroy(mo_t *mo, vka_t *server_vka);
