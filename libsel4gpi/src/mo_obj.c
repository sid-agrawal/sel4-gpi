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

#define DEBUG_ID MO_DEBUG
#define SERVER_ID MOSERVS

int mo_new(mo_t *mo,
           seL4_CPtr *caps,
           vka_object_t *vka_objects,
           uint32_t num_caps,
           vka_t *vka)
{
    assert(caps != NULL);

    mo->frame_caps_in_root_task = malloc(num_caps * sizeof(seL4_CPtr));
    mo->frame_paddrs = malloc(num_caps * sizeof(uintptr_t));

    if (vka_objects)
    {
        mo->vka_objects = malloc(num_caps * sizeof(vka_object_t));
    } else {
        mo->vka_objects = NULL;
    }

    assert(mo->frame_caps_in_root_task != NULL);

    for (int i = 0; i < num_caps; i++)
    {
        // We cannot assert this, may be forging an MO from a reservation
        // that is not fully backed
        // assert(caps[i] != seL4_CapNull);

        mo->frame_caps_in_root_task[i] = caps[i];

        // (XXX) Arya: Should we have a non-debug function for this?
        mo->frame_paddrs[i] = seL4_DebugCapPaddr(caps[i]);

        if (vka_objects)
        {
            mo->vka_objects[i] = vka_objects[i];
        }
    }

    mo->num_pages = num_caps;

    return 0;
}

void mo_dump_rr(mo_t *mo, model_state_t *ms, gpi_model_node_t *pd_node)
{
    gpi_model_node_t *root_node = get_root_node(ms);

    /* Add the MO node */
    // (XXX) Arya: MO does not belong to a resource space! To fix
    gpi_model_node_t *mo_node = add_resource_node(ms, GPICAP_TYPE_MO, 1, mo->id);
    add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, mo_node);

    /* Add the page nodes and relations */
    for (int i = 0; i < mo->num_pages; i++)
    {
        if (mo->frame_caps_in_root_task[i] == 0)
        {
            /**
             * This can happen if there was an ADS deep copy of a region that did not
             * have backing pages for the entire region.
             */
            continue;
        }

        gpi_model_node_t *pmr_node = add_resource_node(ms, GPICAP_TYPE_PMR, 1, mo->frame_paddrs[i]);
        add_edge(ms, GPI_EDGE_TYPE_MAP, mo_node, pmr_node);
        add_edge(ms, GPI_EDGE_TYPE_HOLD, root_node, pmr_node);
    }
}

void mo_destroy(mo_t *mo, vka_t *server_vka)
{
    if (mo->vka_objects == NULL)
    {
        OSDB_PRINTERR("Can't free frames for MO (%d), no associated vka objects\n", mo->id);
        return;
    }

    /* Free all MO frames */
    for (int i = 0; i < mo->num_pages; i++)
    {
        vka_free_object(server_vka, &mo->vka_objects[i]);
    }

    free(mo->frame_caps_in_root_task);
    free(mo->frame_paddrs);
    free(mo->vka_objects);
}