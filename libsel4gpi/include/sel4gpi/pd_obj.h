
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
    uint32_t pd_obj_id;
    // AS_CAP
    // CPU_CAP
    simple_t *simple;
    vka_t *vka;
    vspace_t *vspace;


}pd_t;

int pd_new(pd_t *pd, vka_t *vka);

int pd_load_image(pd_t *pd,
                      vka_t *vka,
                     const char *image_path);

int pd_start(pd_t *pd, vka_t *vka);
