#pragma once

#include <sys/types.h>
#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

#include <sel4gpi/vcpu.h>
#include <sel4gpi/cpu_component.h>
#include <sel4gpi/cpu_client_context.h>
#include <sel4gpi/endpoint_clientapi.h>
#include <sel4gpi/ads_client_context.h>
#include <sel4gpi/mo_client_context.h>
#include <sel4gpi/pd_client_context.h>

/**
 * @brief   Initialize the cpu client.
 *
 * @param server_ep_cap Well known server endpoint cap.
 * @param ret_conn client's connection object
 * @return int 0 on success, -1 on failure.
 */
int cpu_component_client_connect(seL4_CPtr server_ep_cap,
                                 cpu_client_context_t *ret_conn);

/**
 * @brief   Disconnect the cpu client.
 *
 * @param conn
 * @return int 0 on success, -1 on failure.
 */
int cpu_component_client_disconnect(cpu_client_context_t *conn);

/**
 * @brief starts a execution of a CPU object.
 * Requires an ADS and PD have already been binded to the CPU (via cpu_client_config)
 * Assumes that any necessary state (stack or register arguments, entry points)
 * in the ADS has already been set up for the CPU
 *
 * @param conn client connection object
 * @return int 0 on success, 1 on failure.
 */
int cpu_client_start(cpu_client_context_t *conn);

/**
 * @brief Configures a CPU object by binding it to the given ADS and PD
 * NOTE: the fault endpoint's refcount is not increased here for a few reasons:
 *       1) we can't send more than 3 unwrapped caps, and don't have a way to indicate which
 *          EP within the EP component's metadata to refcount -> this could be solved
 *          with other workarounds
 *       2) if the refcount to this EP has reached 0, all PDs holding it have exited,
 *          and nothing will be able to reference it anymore. If the CPU object binded
 *          to this EP is still lying around, it will have to be reconfigured regardless
 *          of whether the EP still exists or not.
 *
 * @param cpu the CPU object to configure
 * @param ads the ADS object to bind
 * @param pd the PD to bind
 * @param ipc_buf_mo MO of the the ipc buf for the cpu (OPTIONAL)
 * @param cnode_guard guard configured for the PD's croot
 * @param fault_ep_position w.r.t the PD's cspace, the fault endpoint (OPTIONAL)
 *                          this is not sent as an unwrapped cap, as the limit is 3
 * @param ipc_buf_addr w.r.t the given ADS, address to IPC buf (OPTIONAL)
 * @param prio scheduler priority of the CPU, default is 0 (OPTIONAL)
 * @return int returns 0 on success, 1 on failure
 */
int cpu_client_config(cpu_client_context_t *cpu,
                      ads_client_context_t *ads,
                      pd_client_context_t *pd,
                      mo_client_context_t *ipc_buf_mo,
                      seL4_Word cnode_guard,
                      seL4_CPtr fault_ep_position,
                      void *ipc_buf_addr,
                      int prio);

/**
 * @brief Change just the vspace of the CPU object
 *
 * @param conn client connection object
 * @param ads_conn ads connection object
 * @return int 0 on success, -1 on failure.
 */
int cpu_client_change_vspace(cpu_client_context_t *conn,
                             ads_client_context_t *ads_conn);

/**
 * @brief elevate privileges of a CPU (e.g. for running a guest)
 *
 * @param conn the CPU to elevate
 * @return int 0 on success, -1 on failure.
 */
int cpu_client_elevate_privileges(cpu_client_context_t *conn);

/**
 * @brief write to the registers of a CPU
 *
 * @param conn the CPU context
 * @param regs the register contents
 * @param num_reg the number of registers to write
 * @param resume if true, resumes the CPU after writing
 * @return int 0 on success, other on failure
 */
int cpu_client_write_registers(cpu_client_context_t *cpu, seL4_UserContext *regs, size_t num_reg, bool resume);

/**
 * @brief read the registers of a CPU
 *
 * @param conn the CPU context
 * @param[out] regs returns the register contents
 * @return int 0 on success, other on failure
 */
int cpu_client_read_registers(cpu_client_context_t *cpu, seL4_UserContext *regs);

/**
 * @brief reads all VCPU registers from the CPU
 *
 * @param cpu the CPU context
 * @param[out] returns a filled in VCPU regs struct
 * @return int 0 on success, other on failure
 */
int cpu_client_read_vcpu_regs(cpu_client_context_t *cpu, vcpu_regs_t *regs);

/* CONVENIENCE FUNCTIONS */
/**
 * @brief sets the TLS base for a CPU obj. The given address should be w.r.t the CPU's ADS
 *
 * @param cpu the target CPU object
 * @param tls_base the TLS base in the target CPU's ADS
 * @return int returns 0 on success, 1 on failure
 */
int cpu_client_set_tls_base(cpu_client_context_t *cpu, void *tls_base);

/**
 * @brief suspends the CPU
 *
 * @param cpu target CPU object
 * @return 0 on success
 */
int cpu_client_suspend(cpu_client_context_t *cpu);

/**
 * @brief Wrapper for seL4_ARM_VCPU_InjectIRQ(). Is a NO-OP if the CPU hasn't been elevated.
 *
 * @param cpu the CPU context
 * @param virq virtual IRQ ID
 * @param prio priority of IRQ to inject
 * @param group IRQ group
 * @param idx VGIC list register
 * @return int 0 on success, other on failure
 */
int cpu_client_inject_irq(cpu_client_context_t *cpu, int virq, int prio, int group, int idx);

/**
 * @brief Wrapper for seL4_ARM_VCPU_AckVPPI(). Is a NO-OP if the CPU hasn't been elevated.
 *
 * @param cpu the CPU context
 * @param irq IRQ ID to ACK
 * @return int 0 on success, other on failure
 */
int cpu_client_ack_vppi(cpu_client_context_t *cpu, uint64_t irq);

/**
 * @brief Binds the CPU and PD to an IRQ handler notification for the given IRQ.
 * If the CPU is listening on any endpoint, it will be unblocked by this notification.
 * Optionally retrieves the handler for the given IRQ, for ACKing future interrupts.
 * Is a NO-OP if the CPU hasn't been elevated.
 *
 * @param cpu the CPU context
 * @param irq the IRQ ID to get a handler for
 * @param badge A badge to differientiate the notification signal
 * @param ret_slot OPTIONAL: IRQ handler for the given IRQ
 * @return int 0 on success, other on failure
 */
int cpu_client_irq_handler_bind(cpu_client_context_t *cpu, int irq, seL4_Word badge, seL4_CPtr *ret_slot);

/**
 * @brief Resume execution of the CPU
 *
 * @param cpu the CPU context
 * @return int 0 on success, other on failure
 */
int cpu_client_resume(cpu_client_context_t *cpu);
