#include <gpivmm/vmm.h>
#include <gpivmm/vgic/virq.h>
#include <gpivmm/vgic/vgic.h>
#include <utils/util.h>

#define SGI_RESCHEDULE_IRQ 0
#define SGI_FUNC_CALL 1
#define PPI_VTIMER_IRQ 27

static void vppi_event_ack(vm_context_t *vm, int irq, void *cookie)
{
    seL4_Error err = seL4_ARM_VCPU_AckVPPI(vm_get_vcpu(vm), irq);
    if (err != seL4_NoError)
    {
        VMM_PRINTERR("Failed to ack VPPI event\n");
    }
}

static void sgi_ack(vm_context_t *vm, int irq, void *cookie) {}

bool virq_controller_init(size_t boot_vcpu_id)
{
    vgic_init();
    // @ivanv: todo, do this dynamically instead of compile time?
#if defined(GIC_V2)
    ZF_LOGE("initialised virtual GICv2 driver");
#elif defined(GIC_V3)
    ZF_LOGE("initialised virtual GICv3 driver\n");
#else
#error "Unsupported GIC version"
#endif

    bool success = vgic_register_irq(boot_vcpu_id, PPI_VTIMER_IRQ, &vppi_event_ack, NULL);
    if (!success)
    {
        ZF_LOGE("Failed to register vCPU virtual timer IRQ: 0x%x\n", PPI_VTIMER_IRQ);
        return false;
    }
    success = vgic_register_irq(boot_vcpu_id, SGI_RESCHEDULE_IRQ, &sgi_ack, NULL);
    if (!success)
    {
        ZF_LOGE("Failed to register vCPU SGI 0 IRQ");
        return false;
    }
    success = vgic_register_irq(boot_vcpu_id, SGI_FUNC_CALL, &sgi_ack, NULL);
    if (!success)
    {
        ZF_LOGE("Failed to register vCPU SGI 1 IRQ");
        return false;
    }

    return true;
}

bool virq_inject(vm_context_t *vm, size_t vcpu_id, int irq)
{
    return vgic_inject_irq(vm, vcpu_id, irq);
}

bool virq_register(size_t vcpu_id, size_t virq_num, virq_ack_fn_t ack_fn, void *ack_data)
{
    return vgic_register_irq(vcpu_id, virq_num, ack_fn, ack_data);
}
