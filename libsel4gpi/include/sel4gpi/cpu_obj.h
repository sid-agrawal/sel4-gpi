
#pragma once

#include <stdint.h>

#include <sel4/sel4.h>

#include <simple/simple.h>
#include <sel4utils/thread.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vspace/vspace.h>
#include <sel4utils/vspace_internal.h>
#include <sel4utils/process.h>

typedef struct _cpu {
    sel4utils_thread_config_t config;
    sel4utils_thread_t obj;
}cpu_t;

/**
 * @brief Start the given CPU
 * 
 * @param cpu cpu object
 * @return int 0 on success, -1 on failure.
 */
int cpu_start(cpu_t *cpu,
              sel4utils_thread_entry_fn entry_point);

/**
 * @brief Config the cpu object
 * 
 * @param cpu cpu object
 * @param vspace vspace i.e. root PT cap
 * @return int 0 on success, -1 on failure.
 */
int cpu_config_vspace(cpu_t *cpu,  vka_t *vka, vspace_t *vspace, seL4_CNode cspace);
