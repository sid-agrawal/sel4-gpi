#include <gpivmm/vmm.h>
#include <gpivmm/virq.h>
#include <gpivmm/linux.h>
#include <sel4gpi/error_handle.h>
#include <cpio/cpio.h>
#include <string.h>

/* guest images are stored here */
extern char _cpio_archive[];
extern char _cpio_archive_end[];

int vmm_init_virq(size_t vcpu_id, virq_ack_fn_t serial_ack_fn)
{
    int error = 0; // unused, to appease error handling macros
    bool success = virq_controller_init(vcpu_id);
    GOTO_IF_COND(!success, "Failed to initialise emulated interrupt controller\n");

    // @ivanv: Note that remove this line causes the VMM to fault if we
    // actually get the interrupt. This should be avoided by making the VGIC driver more stable.
    success = virq_register(vcpu_id, SERIAL_IRQ, serial_ack_fn, NULL);
    WARN_IF_COND(!success, "Failed to register VIRQ handler\n");
err_goto:
    return !success;
}

uintptr_t linux_copy_kernel_image(uintptr_t guest_ram_curr_vspace,
                                  const char *kernel_image_name, UNUSED uint64_t offset)
{
    uint64_t kernel_size = 0;
    uint64_t cpio_len = _cpio_archive_end - _cpio_archive;
    const void *guest_kernel_image = cpio_get_file(_cpio_archive, cpio_len, kernel_image_name, &kernel_size);

    uint64_t dtb_size = 0;
    const void *guest_dtb_image = cpio_get_file(_cpio_archive, cpio_len, LINUX_DTB_NAME, &dtb_size);

    uint64_t initrd_size = 0;
    const void *guest_initrd_image = cpio_get_file(_cpio_archive, cpio_len, LINUX_INITRD_NAME, &initrd_size);

    uintptr_t guest_dtb_curr_vspace = guest_ram_curr_vspace +
                                      ((uintptr_t)GUEST_DTB_VADDR - (uintptr_t)GUEST_RAM_VADDR);
    uintptr_t guest_initrd_curr_vspace = guest_ram_curr_vspace +
                                         ((uintptr_t)GUEST_INIT_RAM_DISK_VADDR - (uintptr_t)GUEST_RAM_VADDR);
    VMM_PRINT("Linux guest ram: %lx, dtb: %lx initrd: %lx\n",
              guest_ram_curr_vspace, guest_dtb_curr_vspace, guest_initrd_curr_vspace);

    uintptr_t kernel_pc_curr_vspace = linux_setup_images(guest_ram_curr_vspace,
                                                         (uintptr_t)guest_kernel_image,
                                                         kernel_size,
                                                         (uintptr_t)guest_dtb_image,
                                                         (uintptr_t)guest_dtb_curr_vspace,
                                                         dtb_size,
                                                         (uintptr_t)guest_initrd_image,
                                                         (uintptr_t)guest_initrd_curr_vspace,
                                                         initrd_size);

    return (uintptr_t)GUEST_RAM_VADDR + (kernel_pc_curr_vspace - guest_ram_curr_vspace);
}

uintptr_t generic_copy_kernel_image(uintptr_t guest_ram_curr_vspace,
                                    const char *kernel_image_name, uint64_t offset)
{
    uint64_t kernel_size = 0;
    uint64_t cpio_len = _cpio_archive_end - _cpio_archive;
    const void *guest_kernel_image = cpio_get_file(_cpio_archive, cpio_len, kernel_image_name, &kernel_size);
    memcpy((char *)(guest_ram_curr_vspace + offset), (char *)guest_kernel_image, kernel_size);

    return (uintptr_t)GUEST_RAM_VADDR + offset;
}
