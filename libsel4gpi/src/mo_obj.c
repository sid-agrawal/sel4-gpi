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

void mo_dump_rr(mo_t *mo, model_state_t *ms, gpi_model_node_t *pd_node)
{
    gpi_model_node_t *root_node = get_root_node(ms);

    /* Add the MO node */
    // (XXX) Arya: MO does not belong to a resource space! To fix
    gpi_model_node_t *mo_node = add_resource_node(ms, GPICAP_TYPE_MO, 1, mo->mo_obj_id);
    add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, mo_node);

    /* Add the page nodes and relations */
    for (int i = 0; i < mo->num_pages; i++)
    {
        if (mo->frame_caps_in_root_task[i].cap == 0) {
            /**
             * This can happen if there was an ADS deep copy of a region that did not 
             * have backing pages for the entire region.
            */
            continue;
        }

        gpi_model_node_t *pmr_node = add_resource_node(ms, GPICAP_TYPE_PMR, 1, mo->frame_caps_in_root_task[i].paddr);
        add_edge(ms, GPI_EDGE_TYPE_MAP, mo_node, pmr_node);
        add_edge(ms, GPI_EDGE_TYPE_HOLD, root_node, pmr_node);
    }
}
