
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>

typedef struct _pd {
    seL4_CPtr cspace_root;
}pd_t;

int pd_new(pd_t *pd, vka_t *vka, void* vaddr, size_t size);
int pd_rm(pd_t *pd, vka_t *vka, void* vaddr, size_t size);
int pd_bind(pd_t *pd, vka_t *vka, seL4_CPtr* cpu_cap);
