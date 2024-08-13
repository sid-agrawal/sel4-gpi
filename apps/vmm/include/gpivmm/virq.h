#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <sel4/sel4.h>
#include <gpivmm/vmm.h>

typedef void (*virq_ack_fn_t)(vm_context_t *vm, int irq, void *cookie);

bool virq_controller_init(size_t boot_vcpu_id);
bool virq_register(size_t vcpu_id, size_t virq_num, virq_ack_fn_t ack_fn, void *ack_data);
bool virq_inject(vm_context_t *vm, size_t vcpu_id, int irq);
