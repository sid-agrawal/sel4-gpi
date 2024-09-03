/**
 * @file cpu_obj.h
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Definitions for an OSmosis CPU resource
 * @version 0.1
 * @date 2024-06-18
 *
 * @copyright Copyright (c) 2024
 *
 */
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
#include <sel4gpi/vcpu.h>
#include <sel4gpi/model_exporting.h>

#define SEL4_USER_CONTEXT_COUNT sizeof(seL4_UserContext) / sizeof(seL4_Word)

typedef struct _cpu
{
    gpi_obj_id_t id;

    vka_object_t tcb;           ///< the TCB object
    void *ipc_buf_addr;         ///< address of the IPC buffer in the TCB's binded ADS
    gpi_obj_id_t ipc_buf_mo;    ///< ID of the IPC buffer's MO
    seL4_CPtr ipc_frame_cap;    ///< the frame cap for the IPC buffer
    gpi_obj_id_t binded_ads_id; ///< ID of the ADS that is binded to the TCB
    void *tls_base;             ///< address of the TLS base (currently unused)
    seL4_CPtr cspace;           ///< cap to the currently binded cspace
    uint64_t cspace_guard;      ///< guard of the currently binded cspace
    seL4_CPtr fault_ep;         ///< currently binded fault endpoint
    seL4_UserContext *reg_ctx;  ///< TCB register values that are to be written, NOT the current values
    vka_object_t vcpu;          ///< VCPU object (only exists if CPU is elevated)
} cpu_t;

/**
 * @brief Start the given CPU. Assumes that the user context (reg_ctx) struct has already been populated
 *
 * @param cpu cpu object
 * @return int 0 on success, 1 on failure.
 */
int cpu_start(cpu_t *cpu);

/**
 * @brief Stop the given CPU.
 *
 * @param cpu cpu object
 * @return int 0 on success, 1 on failure.
 */
int cpu_stop(cpu_t *cpu);

/**
 * @brief Configures the IPC buffer, CSpace, ADS, and Fault endpoint binded to the CPU's TCB
 *
 * @param cpu CPU object
 * @param vspace vspace of the CPU
 * @param root_cnode root cnode for the CPU
 * @param cnode_guard guard on the root cnode
 * @param fault_ep OPTIONAL: endpoint for faults w.r.t to the CPU's cspace
 * @param ipc_buffer_frame OPTIONAL: IPC buffer frame
 * @param ipc_buf_addr OPTIONAL: IPC buffer address
 * @param prio OPTIONAL: prio scheduler priority of the CPU, default is 0 (OPTIONAL)
 * @return int 0 on success, 1 on failure.
 */
int cpu_config_vspace(cpu_t *cpu,
                      vspace_t *vspace,
                      seL4_CNode root_cnode,
                      uint64_t cnode_guard,
                      seL4_CPtr fault_ep,
                      seL4_CPtr ipc_buffer_frame,
                      void *ipc_buf_addr,
                      int prio);

/**
 * @brief Change the vspace of the CPU object
 *
 * @param cpu cpu object
 * @param vspace vspace i.e. root PT cap
 * @return int 0 on success, 1 on failure.
 */
int cpu_change_vspace(cpu_t *cpu,
                      vka_t *vka,
                      vspace_t *vspace);

/**
 * @brief Bind a notification object to a CPU
 *
 * @param cpu cpu object
 * @param notif the notification
 * @return itn 0 on success, error otherwise
 */
int cpu_bind_notif(cpu_t *cpu, seL4_CPtr notif);

/**
 * @brief Create a new cpu object
 *
 * @param cpu cpu object
 * @param vka server VKA, used to allocate tcb
 * @param vspace not used
 * @param arg0 unused
 * @return int 0 on success, 1 on failure.
 */
int cpu_new(cpu_t *cpu,
            vka_t *vka,
            vspace_t *vspace,
            void *arg0);

/**
 * @param cpu cpu object to dump the RR for
 * @param ms pointer to model state
 * @param pd_node the existing node for pd that is being dumped
 * @return gpi_model_node_t * the model state node for the resource
 */
gpi_model_node_t *cpu_dump_rr(cpu_t *cpu, model_state_t *ms, gpi_model_node_t *pd_node);

/**
 * Destroys a VCPU object
 *
 * This does not remove the VCPU from the CPU component registry
 * This function should only be called by the CPU component
 *
 * @param cpu the cpu object
 */
void cpu_destroy(cpu_t *cpu);

/**
 * @brief sets the TLS base for the CPU
 *
 *
 * @param cpu the target CPU object
 * @param tls_base address of the TLS base in the ADS configured for this CPU
 * @param write_reg if true, write the TLS base value into the appropriate register immediately,
 *                  otherwise only the CPU's user context metadata will be set
 * @return int returns 0 on success, 1 on failure
 */
int cpu_set_tls_base(cpu_t *cpu, void *tls_base, bool write_reg);

/**
 * @brief sets the CPU's entry point, stack pointer, and optional argument in its registers
 * intended usage: to start a host PD

 * @param cpu the target CPU
 * @param entry_point address of instruction to start execution at
 * @param init_stack the starting position in the stack (w.r.t CPU's binded ADS)
 * @param arg1 OPTIONAL: value to set in the second argument register (the first is for something else)
 * @return int returns 0 on success, 1 on failure
 */
int cpu_set_pd_entry_regs(cpu_t *cpu, void *entry_point, void *init_stack, seL4_Word arg1);

/**
 * @brief sets the CPU's entry point and optional argument in its registers
 * intended usage: to start a guest PD
 *
 * @param cpu the target CPU
 * @param kernel_entry address of instruction to start execution at
 * @param arg0 OPTIONAL: value to set in the first argument register
 * @return int
 */
int cpu_set_guest_entry_regs(cpu_t *cpu, uintptr_t kernel_entry, seL4_Word arg0);

/**
 * @brief elevates a CPU by creating a VCPU and binding it to the TCB
 * currently only supports ARM arch
 *
 * @param cpu CPU to elevate
 * @return int returns 0 on success, 1 on failure
 */
int cpu_elevate(cpu_t *cpu);

/**
 * @brief Reads the registers of a CPU. Does not store these values in the CPU object
 *
 * @param cpu the CPU object
 * @param[out] reg returns the contents of the registers
 * @return int 0 on success, other on failure
 */
int cpu_read_registers(cpu_t *cpu, seL4_UserContext *regs);

/**
 * @brief Writes to the registers of a CPU. Does not store these values in the CPU object
 *
 * @param cpu the CPU object
 * @param reg contents of the registers to write
 * @param num_reg number of registers to write
 * @param resume if true, will resume the CPU after writing
 * @return int 0 on success, other on failure
 */
int cpu_write_registers(cpu_t *cpu, seL4_UserContext *regs, size_t num_reg, bool resume);

/**
 * @brief Wrapper for seL4_ARM_VCPU_InjectIRQ(). Is a NO-OP if the CPU hasn't been elevated.
 *
 * @param cpu the CPU object
 * @param virq virtual IRQ ID
 * @param prio priority of IRQ to inject
 * @param group IRQ group
 * @param idx VGIC list register
 * @return int 0 on success, other on failure
 */
int cpu_inject_irq(cpu_t *cpu, int virq, int prio, int group, int idx);

/**
 * @brief Wrapper for seL4_ARM_VCPU_AckVPPI(). Is a NO-OP if the CPU hasn't been elevated.
 *
 * @param cpu the CPU object
 * @param irq IRQ ID to ACK
 * @return int 0 on success, other on failure
 */
int cpu_ack_vppi(cpu_t *cpu, uint64_t irq);

/**
 * @brief reads all VCPU registers from the CPU
 *
 * @param cpu the CPU object
 * @param[out] returns a filled in VCPU regs struct
 */
void cpu_read_vcpu_regs(cpu_t *cpu, vcpu_regs_t *regs);

/**
 * @brief Resume execution of the CPU
 *
 * @param cpu the CPU object
 * @return int 0 on success, other on failure
 */
int cpu_resume(cpu_t *cpu);
