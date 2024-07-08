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
#include <osm-vmm/vmm.h>
#include <vmm-common/smoldtb.h>
#include <vmm-common/linux.h>

// @ivanv: ideally we would have none of these hardcoded values
// initrd, ram size come from the DTB
// We can probably add a node for the DTB addr and then use that.
// Part of the problem is that we might need multiple DTBs for the same example
// e.g one DTB for VMM one, one DTB for VMM two. we should be able to hide all
// of this in the build system to avoid doing any run-time DTB stuff.

/*
 * As this is just an example, for simplicity we just make the size of the
 * guest's "RAM" the same for all platforms. For just booting Linux with a
 * simple user-space, 0x10000000 bytes (256MB) is plenty.
 */
#define GUEST_RAM_SIZE 0x10000000

#if defined(BOARD_qemu_arm_virt)
#define GUEST_RAM_PADDR 0x40000000
#define GUEST_RAM_VADDR 0x40000000
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4d700000
#define SERIAL_PADDR 0x9000000
#define GIC_PADDR 0x8040000
#define LINUX_GIC_PADDR 0x8010000
#elif defined(BOARD_rpi4b_hyp)
#define GUEST_DTB_VADDR 0x2e000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#elif defined(BOARD_odroidc2_hyp)
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#elif defined(BOARD_odroidc4)
#define GUEST_RAM_VADDR 0x20000000
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#define ODROID_BUS1 0xff600000
#define ODROID_BUS2 0xff800000
#define ODROID_BUS3 0xffd00000
#define GIC_PADDR 0xffc06000
#define LINUX_GIC_PADDR 0xffc02000
#elif defined(BOARD_imx8mm_evk_hyp)
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4d700000
#else
#error Need to define guest kernel image address and DTB address
#endif

/* For simplicity we just enforce the serial IRQ channel number to be the same
 * across platforms. */
#define SERIAL_IRQ_CH 1

#if defined(BOARD_qemu_arm_virt)
#define SERIAL_IRQ 33
#elif defined(BOARD_odroidc2_hyp) || defined(BOARD_odroidc4)
#define SERIAL_IRQ 225
#elif defined(BOARD_rpi4b_hyp)
#define SERIAL_IRQ 57
#elif defined(BOARD_imx8mm_evk_hyp)
#define SERIAL_IRQ 79
#else
#error Need to define serial interrupt
#endif

#define VM_CNODE_BITS 17

/* Data for the guest's kernel image. */
extern char _guest_kernel_image[];
extern char _guest_kernel_image_end[];
/* Data for the device tree to be passed to the kernel. */
extern char _guest_dtb_image[];
extern char _guest_dtb_image_end[];
/* Data for the initial RAM disk to be passed to the kernel. */
extern char _guest_initrd_image[];
extern char _guest_initrd_image_end[];

// static void handle_fault(void *arg0, void *arg1, void *arg2)
// {
//     VMM_PRINT("in handle_fault\n");
//     sel4gpi_runnable_t *vm_context = (sel4gpi_runnable_t *)arg0;
//     seL4_CPtr fault_ep = (seL4_CPtr)arg1;
//     while (1)
//     {
//         seL4_MessageInfo_t info = seL4_Recv(fault_ep, NULL);
//         VMM_PRINT("fault: %s\n", fault_to_string(seL4_MessageInfo_get_label(info)));
//         // fault_handle_vcpu_exception(vm);
//         // vcpu_print_regs(vm->vcpu.cptr);
//         fault_handle(vm, &info);
//         sel4debug_dump_registers(vm->tcb.cptr);
//     }
// }

static void dtb_on_error(const char *why)
{
    printf("DTB Parser error: %s\n", why);
}

int new_guest(void)
{
    int error = 0;
    pd_config_t *cfg = calloc(1, sizeof(pd_config_t));

    seL4_CPtr pd_rde = sel4gpi_get_rde(GPICAP_TYPE_PD);
    GOTO_IF_COND(pd_rde == seL4_CapNull, "No PD RDE\n");

    seL4_CPtr mo_rde = sel4gpi_get_rde(GPICAP_TYPE_MO);
    GOTO_IF_COND(mo_rde == seL4_CapNull, "No MO RDE\n");

    ads_client_context_t vmr_rde = {.ep = sel4gpi_get_rde(GPICAP_TYPE_VMR)};
    GOTO_IF_COND(vmr_rde.ep == seL4_CapNull, "No VMR RDE\n");

    pd_client_context_t self_pd_conn = sel4gpi_get_pd_conn();

    /* allocate MO for PD's OSmosis data */
    error = mo_component_client_connect(mo_rde, 1, MO_PAGE_BITS, &cfg->osm_data_mo);
    GOTO_IF_ERR(error, "Failed to allocat OSmosis data MO\n");

    sel4gpi_runnable_t runnable = {0};
    /* new PD */
    error = pd_component_client_connect(pd_rde, &cfg->osm_data_mo, &runnable.pd);
    GOTO_IF_ERR(error, "Failed to create new PD\n");

    /* new ADS*/
    seL4_CPtr ads_rde = sel4gpi_get_rde(GPICAP_TYPE_ADS);
    GOTO_IF_COND(ads_rde == seL4_CapNull, "Can't make new ADS, no ADS RDE\n");

    error = ads_component_client_connect(ads_rde, &runnable.ads);
    GOTO_IF_ERR(error, "failed to allocate a new ADS");

    /* new CPU */
    seL4_CPtr cpu_rde = sel4gpi_get_rde(GPICAP_TYPE_CPU);
    GOTO_IF_COND(cpu_rde == seL4_CapNull, "No CPU RDE\n");

    error = cpu_component_client_connect(cpu_rde, &runnable.cpu);
    GOTO_IF_ERR(error, "failed to allocate a new CPU");

    size_t guest_ram_pages = BYTES_TO_SIZE_BITS_PAGES(GUEST_RAM_SIZE, MO_LARGE_PAGE_BITS);
    mo_client_context_t guest_ram_mo = {0};
#ifdef BOARD_qemu_arm_virt
    // On QEMU, there is a special reserved region for VM guest RAM
    void *guest_ram_curr_vspace = sel4gpi_get_vmr_at_paddr(&vmr_rde, guest_ram_pages, NULL, SEL4UTILS_RES_TYPE_GENERIC,
                                                           MO_LARGE_PAGE_BITS, GUEST_RAM_VADDR, &guest_ram_mo);
#elif BOARD_odroidc4
    void *guest_ram_curr_vspace = sel4gpi_get_vmr(&vmr_rde, guest_ram_pages, (void *)GUEST_RAM_VADDR, SEL4UTILS_RES_TYPE_GENERIC,
                                                  MO_LARGE_PAGE_BITS, &guest_ram_mo);
#endif // BOARD_qemu_arm_virt
    GOTO_IF_COND(guest_ram_curr_vspace == NULL, "Failed to reserve region for guest RAM in current ADS\n");

    size_t kernel_size = _guest_kernel_image_end - _guest_kernel_image;
    size_t dtb_size = _guest_dtb_image_end - _guest_dtb_image;
    size_t initrd_size = _guest_initrd_image_end - _guest_initrd_image;

    uintptr_t guest_dtb_curr_vspace = (uintptr_t)guest_ram_curr_vspace + ((uintptr_t)GUEST_DTB_VADDR - (uintptr_t)GUEST_RAM_VADDR);
    uintptr_t guest_initrd_curr_vspace = (uintptr_t)guest_ram_curr_vspace + ((uintptr_t)GUEST_INIT_RAM_DISK_VADDR - (uintptr_t)GUEST_RAM_VADDR);
    VMM_PRINT("guest ram: %lx, dtb: %lx initrd: %lx\n", guest_ram_curr_vspace, guest_dtb_curr_vspace, guest_initrd_curr_vspace);

    uintptr_t kernel_pc_curr_vspace = linux_setup_images((uintptr_t)guest_ram_curr_vspace,
                                                         (uintptr_t)_guest_kernel_image,
                                                         kernel_size,
                                                         (uintptr_t)_guest_dtb_image,
                                                         (uintptr_t)guest_dtb_curr_vspace,
                                                         dtb_size,
                                                         (uintptr_t)_guest_initrd_image,
                                                         (uintptr_t)guest_initrd_curr_vspace,
                                                         initrd_size);

    uintptr_t kernel_pc_vm_vspace = (uintptr_t)GUEST_RAM_VADDR + (kernel_pc_curr_vspace - (uintptr_t)guest_ram_curr_vspace);

    cfg->elevated_cpu = true;
    cfg->ads_cfg.entry_point = (void *)kernel_pc_vm_vspace;

#ifdef BOARD_qemu_arm_virt
    mo_client_context_t serial_dev_mo = {0};
    error = mo_component_client_connect_paddr(mo_rde, 1, MO_PAGE_BITS, SERIAL_PADDR, &serial_dev_mo);
    sel4gpi_add_vmr_config(&cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_GENERIC, (void *)SERIAL_PADDR,
                           NULL, 1, MO_PAGE_BITS, &serial_dev_mo);
#elif BOARD_odroidc4
    size_t two_mb_pages = BYTES_TO_SIZE_BITS_PAGES(MiB_TO_BYTES(2), MO_PAGE_BITS);

    mo_client_context_t bus1_mo = {0};
    error = mo_component_client_connect_paddr(mo_rde, 1, MO_PAGE_BITS, ODROID_BUS1, &bus1_mo);
    GOTO_IF_ERR(error, "Failed to allocate MO for bus 1\n");
    sel4gpi_add_vmr_config(&cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_DEVICE, (void *)ODROID_BUS1,
                           NULL, two_mb_pages, MO_PAGE_BITS, &bus1_mo);

    mo_client_context_t bus2_mo = {0};
    error = mo_component_client_connect_paddr(mo_rde, 1, MO_PAGE_BITS, ODROID_BUS2, &bus2_mo);
    GOTO_IF_ERR(error, "Failed to allocate MO for bus 2\n");
    sel4gpi_add_vmr_config(&cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_DEVICE, (void *)ODROID_BUS2,
                           NULL, two_mb_pages, MO_PAGE_BITS, &bus2_mo);

    mo_client_context_t bus3_mo = {0};
    error = mo_component_client_connect_paddr(mo_rde, 1, MO_PAGE_BITS, ODROID_BUS3, &bus3_mo);
    GOTO_IF_ERR(error, "Failed to allocate MO for bus 3\n");
    sel4gpi_add_vmr_config(&cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_DEVICE, (void *)ODROID_BUS3, NULL,
                           BYTES_TO_SIZE_BITS_PAGES(MiB_TO_BYTES(1), MO_PAGE_BITS), MO_PAGE_BITS, &bus3_mo);
#endif // BOARD_qemu_arm_virt

    mo_client_context_t gic_mo = {0};
    error = mo_component_client_connect_paddr(mo_rde, 1, MO_PAGE_BITS, GIC_PADDR, &gic_mo);
    GOTO_IF_ERR(error, "Could not allocate MO for GIC dev region\n");
    sel4gpi_add_vmr_config(&cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_DEVICE, (void *)LINUX_GIC_PADDR,
                           NULL, 1, MO_PAGE_BITS, &gic_mo);

    sel4gpi_add_vmr_config(&cfg->ads_cfg, GPI_DISJOINT, SEL4UTILS_RES_TYPE_GENERIC, (void *)GUEST_RAM_VADDR,
                           NULL, guest_ram_pages, MO_LARGE_PAGE_BITS, &guest_ram_mo);

    seL4_Word arg = GUEST_DTB_VADDR;
    error = sel4gpi_prepare_pd(cfg, &runnable, 1, &arg);
    GOTO_IF_ERR(error, "Failed to setup VM-PD\n");

    // WIP DTB parsing
    dtb_ops no_malloc_ops = {.on_error = dtb_on_error};

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
    }

    error = sel4gpi_start_pd(&runnable);

    // sel4gpi_configure_thread()

    // pd_client_dump(&runnable.pd, NULL, 0);

    while (1)
    {
        seL4_Yield();
    }

err_goto:
    sel4gpi_config_destroy(cfg);

    return error;
}
