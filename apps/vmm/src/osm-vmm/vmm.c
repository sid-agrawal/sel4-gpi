/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>
#include <stdint.h>
#include <utils/page.h>
#include <sel4/sel4.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/pd_creation.h>
#include <sel4gpi/pd_utils.h>
#include <sel4debug/register_dump.h>
#include <gpivmm/osm-vmm.h>
#include <gpivmm/fault.h>
#include <gpivmm/smoldtb.h>
#include <gpivmm/linux.h>
#include <gpivmm/virq.h>

/* (XXX) Linh: This is meant for the fault handler thread to store its own CPTRs */
typedef struct _vmon_fault_context
{
    ep_client_context_t vm_fault_ep;       ///< Listening endpoint for VM faults
    seL4_CPtr serial_irq_handler;          ///< IRQ handler for serial device
    vm_context_t *guests[MAX_GUEST_COUNT]; ///< List of guests managed by the VMM
} vmon_fault_context_t;

/* Uncomment to use the assembly-embedded kernel images */
// /* Data for the guest's kernel image. */
// extern char _guest_kernel_image[];
// extern char _guest_kernel_image_end[];
// /* Data for the device tree to be passed to the kernel. */
// extern char _guest_dtb_image[];
// extern char _guest_dtb_image_end[];
// /* Data for the initial RAM disk to be passed to the kernel. */
// extern char _guest_initrd_image[];
// extern char _guest_initrd_image_end[];

static vmon_context_t vmon_ctxt;             /* main VMM thread's CPTRs */
static vmon_fault_context_t vmon_fault_ctxt; /* fault VMM thread's CPTRs */

void vm_suspend(vm_context_t *vm)
{
    int err = cpu_client_suspend(&vm->runnable.cpu);
    if (err)
    {
        VMM_PRINTERR("Failed to suspend VMM\n");
    }
}

int vm_write_registers(vm_context_t *vm, bool resume, seL4_UserContext *regs, size_t num_regs)
{
    return cpu_client_write_registers(&vm->runnable.cpu, regs, num_regs, resume);
}

int vm_read_registers(vm_context_t *vm, bool suspend, seL4_UserContext *regs, size_t num_regs)
{
    return cpu_client_read_registers(&vm->runnable.cpu, regs);
}

void vm_dump_registers(vm_context_t *vm)
{
    seL4_UserContext regs = {0};
    cpu_client_read_registers(&vm->runnable.cpu, &regs);
    sel4debug_print_registers(&regs);
}

void vm_dump_vcpu_registers(vm_context_t *vm)
{
    vcpu_regs_t vcpu_regs = {0};
    cpu_client_read_vcpu_regs(&vm->runnable.cpu, &vcpu_regs);
    vcpu_print_regs(&vcpu_regs);
}

uint32_t vm_get_id(vm_context_t *vm)
{
    return vm->id;
}

int vm_inject_irq(vm_context_t *vm, int virq, int prio, int group, int idx)
{
    return cpu_client_inject_irq(&vm->runnable.cpu, virq, prio, group, idx);
}

int vm_ack_vppi(vm_context_t *vm, uint64_t irq)
{
    return cpu_client_ack_vppi(&vm->runnable.cpu, irq);
}

static void serial_ack(vm_context_t *vm, int irq, void *cookie)
{
    /*
     * For now we by default simply ack the serial IRQ, we have not
     * come across a case yet where more than this needs to be done.
     */
    VMM_PRINT("Acking serial interrupt\n");
    seL4_Error error = seL4_IRQHandler_Ack(vmon_fault_ctxt.serial_irq_handler);
    WARN_IF_COND(error, "Failed to ACK serial interrupt, seL4_Error: %d\n", error);
}

static void handle_fault(int argc, char **argv)
{
    int error = ep_client_get_raw_endpoint(&vmon_fault_ctxt.vm_fault_ep);
    if (error)
    {
        VMM_PRINTERR("Failed to get raw fault endpoint for listening, exiting\n");
        return;
    }

    vm_context_t *vm = NULL;
    seL4_Word badge = 0;
    seL4_MessageInfo_t info = {0};
    uint32_t vm_id = 0;
    while (1)
    {
        info = seL4_Recv(vmon_fault_ctxt.vm_fault_ep.raw_endpoint, &badge);
        if (FAULT_BADGE_FLAG & badge)
        {
            vm_id = (uint32_t)(badge & ~FAULT_BADGE_FLAG);
            if (vm_id == 0 || vm_id >= MAX_GUEST_COUNT)
            {
                VMM_PRINTERR("Fault received from invalid VM: %u\n", vm_id);
                continue;
            }

            VMM_PRINTV("VM %u fault: %s\n", vm_id, fault_to_string(seL4_MessageInfo_get_label(info)));

            vm = vmon_fault_ctxt.guests[vm_id];
            assert(vm != NULL);

            fault_handle(vm, &info);
        }
        else if (badge & SERIAL_IRQ_BIT)
        {
            VMM_PRINT("Got serial IRQ\n");
            if (!virq_inject(vm, GUEST_VCPU_ID, SERIAL_IRQ))
            {
                VMM_PRINTERR("Failed to inject serial IRQ %d into vCPU %d\n", SERIAL_IRQ, GUEST_VCPU_ID);
            }
        }
        else
        {
            VMM_PRINTERR("Received Unexpected fault or IRQ notification for badge: %lx\n", badge);
        }
    }
}

static void dtb_on_error(const char *why)
{
    printf("DTB Parser error: %s\n", why);
}

int osm_vmm_init(void)
{
    int error = 0;

    memset(&vmon_ctxt, 0, sizeof(vmon_context_t));
    vmon_ctxt.guest_id_counter = 1;

    error = vmm_init_virq(GUEST_VCPU_ID, &serial_ack);
    GOTO_IF_ERR(error, "Failed to initialize VIRQ controller\n");

    error = ep_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_EP), &vmon_ctxt.vm_fault_ep);
    GOTO_IF_ERR(error, "Failed to allocate fault EP for VMM to listen on\n");

    ep_client_context_t fault_handler_ep = sel4gpi_get_fault_ep_conn(); // this is the handler of the VMM's faults
    pd_config_t *t_cfg = sel4gpi_configure_thread(handle_fault, &fault_handler_ep, &vmon_ctxt.fault_thread_runnable);
    GOTO_IF_COND(t_cfg == NULL, "Couldn't allocate VMM's fault handler thread\n");

    /* Give the fault listening EP to the fault thread */
    error = pd_client_send_cap(&vmon_ctxt.fault_thread_runnable.pd,
                               vmon_ctxt.vm_fault_ep.ep,
                               &vmon_fault_ctxt.vm_fault_ep.ep);
    GOTO_IF_ERR(error, "Failed to send fault listening EP to thread\n");

    t_cfg->cpu_prio = seL4_MinPrio + 1;
    error = sel4gpi_prepare_pd(t_cfg, &vmon_ctxt.fault_thread_runnable, 0, NULL);
    GOTO_IF_ERR(error, "Failed to prepare VMM's fault handler thread\n");

    error = sel4gpi_start_pd(&vmon_ctxt.fault_thread_runnable);
    GOTO_IF_ERR(error, "Failed to start VMM's fault handler thread\n");

    /* bind the fault thread to the serial IRQ handler */
    error = cpu_client_irq_handler_bind(&vmon_ctxt.fault_thread_runnable.cpu,
                                        SERIAL_IRQ, SERIAL_IRQ_BIT,
                                        &vmon_fault_ctxt.serial_irq_handler);
    GOTO_IF_ERR(error, "Failed to bind VMM's CPU to the serial IRQ handler\n");

err_goto:
    return error;
}

static int configure_guest_pd(pd_config_t *vm_cfg, const char *kernel_img_name, uint64_t kernel_img_offset,
                              copy_kernel_image_fn_t kernel_img_cp_fn, uint64_t guest_id)
{
    int error;
    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
    GOTO_IF_COND(mo_rde == seL4_CapNull, "No MO RDE\n");

    seL4_CPtr vmr_rde = sel4gpi_get_bound_vmr_rde();
    GOTO_IF_COND(vmr_rde == seL4_CapNull, "No VMR RDE\n");

    size_t guest_ram_pages = BYTES_TO_SIZE_BITS_PAGES(GUEST_RAM_SIZE, MO_LARGE_PAGE_BITS);
    mo_client_context_t guest_ram_mo = {0};
    void *guest_ram_curr_vspace = NULL;
#ifdef BOARD_qemu_arm_virt
    // On QEMU, there is a special reserved region for VM guest RAM
    guest_ram_curr_vspace = sel4gpi_get_vmr_at_paddr(vmr_rde, guest_ram_pages, NULL, SEL4UTILS_RES_TYPE_GENERIC,
                                                     MO_LARGE_PAGE_BITS, QEMU_VM_RESERVE_PADDR, &guest_ram_mo);
#elif BOARD_odroidc4
    guest_ram_curr_vspace = sel4gpi_get_vmr(vmr_rde, guest_ram_pages, (void *)GUEST_RAM_VADDR,
                                            SEL4UTILS_RES_TYPE_GENERIC, MO_LARGE_PAGE_BITS, &guest_ram_mo);
#endif // BOARD_qemu_arm_virt
    GOTO_IF_COND(guest_ram_curr_vspace == 0, "Failed to reserve region for guest RAM in current ADS\n");

    uintptr_t kernel_pc_vm_vspace = kernel_img_cp_fn((uintptr_t)guest_ram_curr_vspace,
                                                     kernel_img_name, kernel_img_offset);

/* setup UART device */
#ifdef BOARD_qemu_arm_virt
    mo_client_context_t serial_dev_mo = {0};
    error = mo_component_client_connect_paddr(mo_rde, 1, MO_PAGE_BITS, SERIAL_PADDR, &serial_dev_mo);
    sel4gpi_add_vmr_config(&vm_cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_GENERIC, (void *)SERIAL_PADDR,
                           NULL, 1, MO_PAGE_BITS, &serial_dev_mo);
#elif BOARD_odroidc4
    size_t two_mb_pages = BYTES_TO_SIZE_BITS_PAGES(MiB_TO_BYTES(2), MO_PAGE_BITS);

    mo_client_context_t bus1_mo = {0};
    error = mo_component_client_connect_paddr(mo_rde, 1, MO_PAGE_BITS, ODROID_BUS1, &bus1_mo);
    GOTO_IF_ERR(error, "Failed to allocate MO for bus 1\n");
    sel4gpi_add_vmr_config(&vm_cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_DEVICE, (void *)ODROID_BUS1,
                           NULL, two_mb_pages, MO_PAGE_BITS, &bus1_mo);

    mo_client_context_t bus2_mo = {0};
    error = mo_component_client_connect_paddr(mo_rde, 1, MO_PAGE_BITS, ODROID_BUS2, &bus2_mo);
    GOTO_IF_ERR(error, "Failed to allocate MO for bus 2\n");
    sel4gpi_add_vmr_config(&vm_cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_DEVICE, (void *)ODROID_BUS2,
                           NULL, two_mb_pages, MO_PAGE_BITS, &bus2_mo);

    mo_client_context_t bus3_mo = {0};
    error = mo_component_client_connect_paddr(mo_rde, 1, MO_PAGE_BITS, ODROID_BUS3, &bus3_mo);
    GOTO_IF_ERR(error, "Failed to allocate MO for bus 3\n");
    sel4gpi_add_vmr_config(&vm_cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_DEVICE, (void *)ODROID_BUS3, NULL,
                           BYTES_TO_SIZE_BITS_PAGES(MiB_TO_BYTES(1), MO_PAGE_BITS), MO_PAGE_BITS, &bus3_mo);
#endif // BOARD_qemu_arm_virt

    if (strcmp(kernel_img_name, LINUX_KERNEL_NAME) == 0)
    {
        /* setup GIC */
        mo_client_context_t gic_mo = {0};
        error = mo_component_client_connect_paddr(mo_rde, 1, MO_PAGE_BITS, GIC_PADDR, &gic_mo);
        GOTO_IF_ERR(error, "Could not allocate MO for GIC dev region\n");
        sel4gpi_add_vmr_config(&vm_cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_DEVICE, (void *)LINUX_GIC_PADDR,
                               NULL, 1, MO_PAGE_BITS, &gic_mo);
    }

    /* guest RAM config */
    sel4gpi_add_vmr_config(&vm_cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_GENERIC, (void *)GUEST_RAM_VADDR,
                           NULL, guest_ram_pages, MO_LARGE_PAGE_BITS, &guest_ram_mo);

    vm_cfg->elevated_cpu = true;
    vm_cfg->ads_cfg.entry_point = (void *)kernel_pc_vm_vspace;
    vm_cfg->fault_ep = vmon_ctxt.vm_fault_ep;
    vm_cfg->fault_ep_badge = FAULT_BADGE_FLAG | guest_id;
    vm_cfg->cpu_prio = seL4_MinPrio + 1;
    vm_cfg->link_with_current = true;

err_goto:
    return error;
}

uint32_t osm_new_guest(const char *kernel_image)
{
    int error = 0;
    uint32_t guest_id = vmon_ctxt.guest_id_counter;
    GOTO_IF_COND(vmon_ctxt.guest_id_counter >= MAX_GUEST_COUNT, "Maximum number of guests started\n");
    vm_context_t *vm = calloc(1, sizeof(vm_context_t));

    seL4_Word *args = NULL;
    int argc = 0;
    uint64_t kernel_img_offset = 0;
    copy_kernel_image_fn_t cp_fn = NULL;
    if (strcmp(kernel_image, LINUX_KERNEL_NAME) == 0)
    {
        argc = 1;
        seL4_Word dtb_arg = GUEST_DTB_VADDR;
        args = &dtb_arg;
        cp_fn = linux_copy_kernel_image;
    }
    else
    {
        if (strcmp(kernel_image, HELLO_KERNEL_NAME) == 0)
        {
            kernel_img_offset = HELLO_KERNEL_PC_OFFSET;
        }
        cp_fn = generic_copy_kernel_image;
    }

    pd_config_t *vm_cfg = sel4gpi_new_runnable(true, true, &vm->runnable);
    error = configure_guest_pd(vm_cfg, kernel_image, kernel_img_offset, cp_fn, (uint64_t)guest_id);
    GOTO_IF_ERR(error, "Failed to setup guest device regions\n");

    error = sel4gpi_prepare_pd(vm_cfg, &vm->runnable, argc, args);
    GOTO_IF_ERR(error, "Failed to setup VM-PD\n");

    // WIP DTB parsing
    /* dtb_ops no_malloc_ops = {.on_error = dtb_on_error};

    dtb_init((uintptr_t)guest_dtb_curr_vspace, no_malloc_ops);

    dtb_node *alias_node = dtb_find("aliases");
    printf("node exists? %d\n", alias_node != NULL);

    if (alias_node)
    {
        dtb_prop *serial_alias = dtb_find_prop(alias_node, "serial0");
        const char *serial_node_name = dtb_read_string(serial_alias, 0);
        printf("serial node name: %s\n", serial_node_name);

        dtb_node *serial_node = dtb_find(serial_node_name);
        printf("node exists? %d\n", serial_node != NULL);

        if (serial_node)
        {
            dtb_prop *reg_prop = dtb_find_prop(serial_node, "reg");
            size_t num_vals = dtb_read_prop_values(reg_prop, 2, NULL);
            printf("num_vals: %d\n", num_vals);
            size_t reg_vals[num_vals];
            dtb_read_prop_values(reg_prop, 2, reg_vals);

            for (size_t i = 0; i < num_vals; i++)
            {
                printf("vals: %zX\n", reg_vals[i]);
            }
        }
    } */

    /* Just in case there is already an interrupt available to handle, we ack it here. */
    serial_ack(vm, SERIAL_IRQ, NULL);

    error = sel4gpi_start_pd(&vm->runnable);
    GOTO_IF_ERR(error, "Failed to start VM\n");

#ifdef GPI_EXTRACT_MODEL
    pd_client_dump(&runnable.pd, NULL, 0);
#endif

    vmon_fault_ctxt.guests[guest_id] = vm;
    vmon_ctxt.guest_id_counter++;
    vm->id = guest_id;

    /* replace each CPTR in the VM context with a CPTR relative to the fault thread's CSpace  */
    error = pd_client_send_cap(&vmon_ctxt.fault_thread_runnable.pd, vm->runnable.cpu.ep, &vm->runnable.cpu.ep);
    error |= pd_client_send_cap(&vmon_ctxt.fault_thread_runnable.pd, vm->runnable.ads.ep, &vm->runnable.ads.ep);
    error |= pd_client_send_cap(&vmon_ctxt.fault_thread_runnable.pd, vm->runnable.pd.ep, &vm->runnable.pd.ep);
    GOTO_IF_ERR(error, "Failed to copy VM caps to fault handler thread, future resource operations will fail\n");

err_goto:
    // (XXX) Linh: we should be cleaning up intermediate allocations on failure
    sel4gpi_config_destroy(vm_cfg);

    return error ? 0 : guest_id;
}
