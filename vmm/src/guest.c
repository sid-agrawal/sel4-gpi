#include "vcpu.h"
#include "guest.h"
#include <stdbool.h>
#include "vmm/vmm.h"
#include <sel4utils/sel4_zf_logif.h>
#include <utils/zf_log.h>

bool guest_start(size_t boot_vcpu_id, uintptr_t kernel_pc, uintptr_t dtb, uintptr_t initrd) {
    /*
     * Set the TCB registers to what the virtual machine expects to be started with.
     * You will note that this is currently Linux specific as we currently do not support
     * any other kind of guest. However, even though the library is open to supporting other
     * guests, there is no point in prematurely generalising this code.
     */
    seL4_UserContext regs = {0};
    regs.x0 = dtb;
    regs.spsr = 5; // PMODE_EL1h
    regs.pc = kernel_pc;
    /* Write out all the TCB registers */
    seL4_Error err = seL4_TCB_WriteRegisters(
        BASE_VM_TCB_CAP + boot_vcpu_id,
        false, // We'll explcitly start the guest below rather than in this call
        0, // No flags
        SEL4_USER_CONTEXT_SIZE, // Writing to x0, pc, and spsr // @ivanv: for some reason having the number of registers here does not work... (in this case 2)
        &regs
    );
    assert(err == seL4_NoError);
    if (err != seL4_NoError) {
        ZF_LOGE("Failed to write registers to boot vCPU's TCB (id is 0x%lx), error is: 0x%d\n", boot_vcpu_id, err);
        return false;
    }
    ZF_LOGI("starting guest at 0x%lx, DTB at 0x%lx, initial RAM disk at 0x%lx\n",
        regs.pc, regs.x0, initrd);
    /* Restart the boot vCPU to the program counter of the TCB associated with it */
    // microkit_vm_restart(boot_vcpu_id, regs.pc);
    seL4_UserContext ctxt = {0};
    // memzero(&ctxt, sizeof(seL4_UserContext));
    ctxt.pc = regs.pc;
    err = seL4_TCB_WriteRegisters(
        BASE_VM_TCB_CAP + boot_vcpu_id,
        true,
        0, /* No flags */
        1, /* writing 1 register */
        &ctxt
    );

    ZF_LOGF_IFERR(err, "Failed to write registers");

    return true;
}

void guest_stop(size_t boot_vcpu_id) {
    ZF_LOGI("Stopping guest\n");
    // microkit_vm_stop(boot_vcpu_id); // XXX
    ZF_LOGI("Stopped guest\n");
}

bool guest_restart(size_t boot_vcpu_id, uintptr_t guest_ram_vaddr, size_t guest_ram_size) {
    ZF_LOGI("Attempting to restart guest\n");
    // First, stop the guest
    // microkit_vm_stop(boot_vcpu_id); // XXX
    ZF_LOGI("Stopped guest\n");
    // Then, we need to clear all of RAM
    ZF_LOGI("Clearing guest RAM\n");
    memset((char *)guest_ram_vaddr, 0, guest_ram_size);
    // Copy back the images into RAM
    // bool success = guest_init_images();
    // if (!success) {
    //     ZF_LOGE("Failed to initialise guest images\n");
    //     return false;
    // }
    // vcpu_reset(boot_vcpu_id); // XXX
    // Now we need to re-initialise all the VMM state
    // vmm_init();
    // linux_start(GUEST_VCPU_ID, kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
    ZF_LOGI("Restarted guest\n");
    return true;
}
