
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

typedef struct _mo_frame
{
    seL4_CPtr cap;
    seL4_Word paddr;
} mo_frame_t;

typedef struct _mo
{
    uint64_t mo_obj_id;
    mo_frame_t *frame_caps_in_root_task;
    uint32_t num_pages;
} mo_t;

/**
 * @brief Create a new cpu object
 *
 * @param mo mo object
 * @param caps array of caps to be stored in the mo object
 * @param num_caps number of caps in the array
 * @param vka vka object
 * @return int 0 on success, -1 on failure.
 */
int mo_new(mo_t *mo,
           mo_frame_t *frame_caps,
           uint32_t num_caps,
           vka_t *vka);

/**
 * @param mo mo object to dump the RR for
 * @param ms pointer to model state
 * @param pd_node the existing node for pd that is being dumped
 * @return void
 */
void mo_dump_rr(mo_t *mo, model_state_t *ms, gpi_model_node_t *pd_node);
