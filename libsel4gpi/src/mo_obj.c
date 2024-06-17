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
#include <sel4gpi/error_handle.h>
#include <sel4gpi/pd_component.h>
#include <sel4gpi/gpi_server.h>

#define DEBUG_ID MO_DEBUG
#define SERVER_ID MOSERVS

int mo_new(mo_t *mo,
           vka_t *vka,
           vspace_t *vspace,
           int num_pages)
{
    int error = 0;

    mo->num_pages = num_pages;
    mo->frame_caps_in_root_task = malloc(num_pages * sizeof(seL4_CPtr));
    mo->frame_paddrs = malloc(num_pages * sizeof(uintptr_t));
    mo->vka_objects = malloc(num_pages * sizeof(vka_object_t));
    GOTO_IF_COND(mo->frame_caps_in_root_task == NULL || mo->frame_paddrs == NULL || mo->vka_objects == NULL,
                 "malloc ran out of memory to allocate MO with %d frames\n", num_pages);

    /* Allocate frames */
    for (int i = 0; i < num_pages; i++)
    {
        error = vka_alloc_frame_maybe_device(get_mo_component()->server_vka,
                                             seL4_PageBits,
                                             false,
                                             &mo->vka_objects[i]);
        assert(error == 0);
        GOTO_IF_COND(error, "failed to allocate page for MO\n");
        mo->frame_caps_in_root_task[i] = mo->vka_objects[i].cptr;
        mo->frame_paddrs[i] = vka_object_paddr(vka, &mo->vka_objects[i]);
    }

    /* The root task holds the MO by default */
    error = pd_add_resource_by_id(get_gpi_server()->rt_pd_id, GPICAP_TYPE_MO, get_mo_component()->space_id, mo->id,
                                  seL4_CapNull, seL4_CapNull, seL4_CapNull);

    SERVER_GOTO_IF_ERR(error, "Failed to add new MO to root task\n");

err_goto:
    return error;
}

void mo_dump_rr(mo_t *mo, model_state_t *ms, gpi_model_node_t *pd_node)
{
    gpi_model_node_t *root_node = get_root_node(ms);

    // Add the MO resource space
    gpi_model_node_t *mo_space_node = add_resource_space_node(ms, GPICAP_TYPE_MO, get_mo_component()->space_id);

    /* Add the MO node */
    gpi_model_node_t *mo_node = add_resource_node(ms, GPICAP_TYPE_MO, 1, mo->id);
    add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, mo_node);
    add_edge(ms, GPI_EDGE_TYPE_SUBSET, mo_node, mo_space_node);

    /* Add the page nodes and relations */
    int num_pages = 0;
    for (int i = 0; i < mo->num_pages; i++)
    {
        if (mo->frame_caps_in_root_task[i] == 0)
        {
            /**
             * This can happen if there was an ADS deep copy of a region that did not
             * have backing pages for the entire region.
             * (XXX) Arya: we should be able to remove this if we fix the ADS copy
             */
            continue;
        }

        // Do not add the physical pages as relations, just count them
        // and store the count in the MO node
        num_pages++;
    }

    // Set the number of pages as extra data on the MO
    char num_pages_str[CSV_MAX_STRING_SIZE];
    snprintf(num_pages_str, CSV_MAX_STRING_SIZE, "%d", num_pages);
    set_node_extra(mo_node, num_pages_str);
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
        // Check if the cap is the last copy - it should be
        // If not, it will cause errors with the VKA later
#ifdef CONFIG_DEBUG_BUILD
        if (OSMOSIS_DEBUG && !seL4_DebugCapIsLastCopy(mo->vka_objects[i].cptr))
        {
            OSDB_PRINTERR("Freeing frame (%p) for MO (%d), cap (%d) is not last copy\n",
                          mo->frame_paddrs[i], mo->id, mo->vka_objects[i].cptr);
        }
#endif
        vka_free_object(server_vka, &mo->vka_objects[i]);
    }

    free(mo->frame_caps_in_root_task);
    free(mo->frame_paddrs);
    free(mo->vka_objects);
}