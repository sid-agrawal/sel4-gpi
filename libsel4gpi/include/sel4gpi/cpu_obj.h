
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

typedef struct _cpu
{
    sel4utils_thread_t thread; // storage for some commonly used fields
    uint64_t cpu_obj_id;
    uint64_t binded_ads_id;
    void *tls_base;
    // the currently binded cspace, could potentially change if reconfigured
    seL4_CPtr cspace;
    seL4_Word cspace_guard;
    seL4_CPtr fault_ep;
} cpu_t;

/**
 * @brief Start the given CPU
 *
 * @param cpu cpu object
 * @return int 0 on success, -1 on failure.
 */
int cpu_start(cpu_t *cpu,
              sel4utils_thread_entry_fn entry_point,
              seL4_Word init_stack,
              seL4_Word arg0);

/**
 * @brief Config the cpu object
 *
 * @param cpu cpu object
 * @param vspace vspace i.e. root PT cap
 * @return int 0 on success, -1 on failure.
 */

/**
 * @brief
 *
 * @param cpu cpu opject
 * @param vka
 * @param vspace
 * @param cspace
 * @param fault_ep endpoint for fault handling
 * @param ipc_buffer true if we want to create an IPC buffer for the TCB
 * @return int
 */
int cpu_config_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace,
                      seL4_CNode root_cnode,
                      seL4_Word cnode_guard,
                      seL4_CPtr fault_ep,
                      seL4_CPtr ipc_buffer_frame,
                      seL4_Word ipc_buf_addr);

/**
 * @brief Change the vspace of the CPU object
 *
 * @param cpu cpu object
 * @param vspace vspace i.e. root PT cap
 * @return int 0 on success, -1 on failure.
 */
int cpu_change_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace);
/**
 * @brief Create a new cpu object
 *
 * @param cpu cpu object
 * @return int 0 on success, -1 on failure.
 */
int cpu_new(cpu_t *cpu,
            vka_t *vka);


/**
 * @param cpu cpu object to dump the RR for
 * @param ms pointer to model state
 * @param pd_node the existing node for pd that is being dumped
 * @return void
 */
void cpu_dump_rr(cpu_t *cpu, model_state_t *ms, gpi_model_node_t *pd_node);
