#pragma once

#include <stddef.h>
#include <stdint.h>
#include "arch/aarch64/vgic/vgic.h"
#include "arch/aarch64/linux.h"
#include "arch/aarch64/fault.h"
#include "guest.h"
#include "virq.h"
#include "tcb.h"
#include "vcpu.h"
#include <vka/vka.h>

#define GUEST_VCPU_ID 0
#define GUEST_NUM_VCPUS 1

void vm_init(seL4_IRQHandler irq_handler, vka_t *vka);
