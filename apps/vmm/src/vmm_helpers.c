#include <gpivmm/vmm.h>
#include <gpivmm/virq.h>
#include <sel4gpi/error_handle.h>

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
