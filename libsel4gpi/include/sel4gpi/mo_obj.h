
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

/**
 * Represents at attachment of a memory object
 * to an ADS
 *
 * Each attach of the same MO requires a copy
 * of the frame capabilities
 * */
typedef struct _attach_node
{
    uint32_t ads_obj_id;
    void *vaddr;
    seL4_CPtr *frame_caps;

    struct _attach_node *next;
} attach_node_t;

typedef struct _mo
{
    uint64_t mo_obj_id;
    seL4_CPtr *frame_caps_in_root_task;
    uint32_t num_pages;
    attach_node_t *attach_nodes;
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
           seL4_CPtr *frame_caps,
           uint32_t num_caps,
           vka_t *vka);
