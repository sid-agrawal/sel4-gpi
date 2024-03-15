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
#include <sel4gpi/model_exporting.h>

int mo_new(mo_t *mo,
           mo_frame_t *caps,
           uint32_t num_caps,
           vka_t *vka)
{
    assert(caps != NULL);

    mo->frame_caps_in_root_task = malloc(num_caps * sizeof(mo_frame_t));
    assert(mo->frame_caps_in_root_task != NULL);

    for (int i = 0; i < num_caps; i++)
    {
        assert(caps[i].cap != seL4_CapNull);
        mo->frame_caps_in_root_task[i] = caps[i];
    }

    mo->num_pages = num_caps;

    return 0;
}

void mo_dump_rr(mo_t *mo, model_state_t *ms)
{
    char mo_res_id[CSV_MAX_STRING_SIZE];
    make_res_id(mo_res_id, GPICAP_TYPE_MO, mo->mo_obj_id);
    add_resource(ms, cap_type_to_str(GPICAP_TYPE_MO), mo_res_id);

    for (int i = 0; i < mo->num_pages; i++)
    {
        char page_res_id[CSV_MAX_STRING_SIZE];
        make_phys_res_id(page_res_id, mo->mo_obj_id, mo->frame_caps_in_root_task[i].paddr, "PMR");
        add_resource(ms, "PhysicalPage", page_res_id);
        add_resource_depends_on(ms, mo_res_id, page_res_id);
    }
}
