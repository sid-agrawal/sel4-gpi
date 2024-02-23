/**
 * @file mo_obj.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the methods to manipulate the Memory object
 * @version 0.1
 * @date 2023-01-07
 *
 * @copyright Copyright (c) 2024
 *
 */

#include <sel4utils/process.h>
#include <sel4utils/vspace.h>
#include <sel4utils/util.h>
#include <sel4utils/helpers.h>

#include <sel4gpi/mo_component.h>
#include <sel4gpi/mo_obj.h>
#include <sel4gpi/debug.h>

int mo_new(mo_t *mo,
           seL4_CPtr *caps,
           uint32_t num_caps,
           vka_t *vka)
{
    assert(caps != NULL);

    mo->frame_caps_in_root_task = malloc(num_caps * sizeof(seL4_CPtr));
    assert(mo->frame_caps_in_root_task != NULL);

    for (int i = 0; i < num_caps; i++)
    {
        assert(caps[i] != seL4_CapNull);
        mo->frame_caps_in_root_task[i] = caps[i];
    }

    mo->num_pages = num_caps;

    return 0;
}