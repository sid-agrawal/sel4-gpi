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
#include <vka/object.h>

#include <sel4gpi/mo_component.h>
#include <sel4gpi/mo_obj.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/model_exporting.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/pd_component.h>
#include <sel4gpi/gpi_server.h>

#define DEBUG_ID MO_DEBUG
#define SERVER_ID MOSERVS

static int alloc_frames(vka_t *vka, mo_t *mo, uint32_t num_pages, size_t page_bits)
{
    int error = 0;
    for (int i = 0; i < num_pages; i++)
    {
        error = vka_alloc_frame_maybe_device(vka,
                                             page_bits,
                                             false,
                                             &mo->vka_objects[i]);
        SERVER_GOTO_IF_ERR(error, "failed to allocate page for MO\n");
        mo->frame_caps_in_root_task[i] = mo->vka_objects[i].cptr;
        mo->frame_paddrs[i] = vka_object_paddr(vka, &mo->vka_objects[i]);
    }

err_goto:
    // TODO: free frames if something failed mid-allocation
    return error;
}

static int alloc_frames_at_paddr(vka_t *vka, mo_t *mo, uint32_t num_pages, size_t page_bits, uintptr_t paddr)
{
    int error = 0;
    uintptr_t curr = paddr;

    for (size_t i = 0; i < num_pages; i++)
    {
        error = vka_alloc_frame_at(vka, page_bits, curr, &mo->vka_objects[i]);
        SERVER_GOTO_IF_ERR(error, "failed to allocate page for MO\n");
        mo->frame_caps_in_root_task[i] = mo->vka_objects[i].cptr;
        mo->frame_paddrs[i] = curr; // is it possible for VKA to succeed and return a different paddr?
        curr += SIZE_BITS_TO_BYTES(page_bits);
    }

err_goto:
    // TODO: free frames if something failed mid-allocation
    return error;
}

int mo_new(mo_t *mo,
           vka_t *vka,
           vspace_t *vspace,
           mo_new_args_t *alloc_args)
{
    int error = 0;

    mo->num_pages = alloc_args->num_pages;
    mo->page_bits = alloc_args->page_bits;
    mo->frame_caps_in_root_task = calloc(alloc_args->num_pages, sizeof(seL4_CPtr));
    mo->frame_paddrs = calloc(alloc_args->num_pages, sizeof(uintptr_t));
    mo->vka_objects = calloc(alloc_args->num_pages, sizeof(vka_object_t));
    SERVER_GOTO_IF_COND(mo->frame_caps_in_root_task == NULL || mo->frame_paddrs == NULL || mo->vka_objects == NULL,
                        "malloc ran out of memory to allocate MO with %d frames\n", alloc_args->num_pages);

    /* Allocate frames */
    if (alloc_args->paddr)
    {
        error = alloc_frames_at_paddr(vka, mo, alloc_args->num_pages, alloc_args->page_bits, alloc_args->paddr);
    }
    else
    {
        error = alloc_frames(vka, mo, alloc_args->num_pages, alloc_args->page_bits);
    }
    SERVER_GOTO_IF_ERR(error, "Failed to allocate MO frames\n");

    /* The root task holds the MO by default */
    error = pd_add_resource_by_id(get_gpi_server()->rt_pd_id,
                                  make_res_id(GPICAP_TYPE_MO, get_mo_component()->space_id, mo->id),
                                  seL4_CapNull, seL4_CapNull, seL4_CapNull);

    SERVER_GOTO_IF_ERR(error, "Failed to add new MO to root task\n");

err_goto:
    return error;
}

gpi_model_node_t *mo_dump_rr(mo_t *mo, model_state_t *ms, gpi_model_node_t *pd_node)
{
    gpi_model_node_t *root_node = get_root_node(ms);

    // Add the MO resource space
    gpi_model_node_t *mo_space_node = add_resource_space_node(ms, GPICAP_TYPE_MO, get_mo_component()->space_id, false);
    add_edge(ms, GPI_EDGE_TYPE_HOLD, root_node, mo_space_node); // the RT holds this resource space

    /* Add the MO node */
    gpi_res_id_t mo_id = make_res_id(GPICAP_TYPE_MO, get_mo_component()->space_id, mo->id);
    gpi_model_node_t *mo_node = get_resource_node(ms, mo_id);

    if (!mo_node)
    {
        mo_node = add_resource_node(ms, mo_id, false);
    }

    if (!mo_node->extracted)
    {
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

        // Set the number of pages, page size and starting phys addr as extra data on the MO
        char extra_str[CSV_MAX_STRING_SIZE];
        snprintf(extra_str, CSV_MAX_STRING_SIZE, "0x%lx_%d_%zu", mo->frame_paddrs[0], num_pages, mo->page_bits);
        set_node_extra(mo_node, extra_str);

        mo_node->extracted = true;
    }
    return mo_node;
}

void mo_destroy(mo_t *mo, vka_t *server_vka)
{
    if (mo->vka_objects == NULL)
    {
        OSDB_PRINTWARN("Can't free frames for MO (%d), no associated vka objects\n", mo->id);
        return;
    }

    /* Free all MO frames */
    for (int i = 0; i < mo->num_pages; i++)
    {
        // Check if the cap is the last copy - it should be
        // If not, it will cause errors with the VKA later
#ifdef CONFIG_DEBUG_BUILD
        if (!seL4_DebugCapIsLastCopy(mo->vka_objects[i].cptr))
        {
            OSDB_PRINTERR("Freeing frame (%lx) for MO (%d), cap (%ld) is not last copy\n",
                          mo->frame_paddrs[i], mo->id, mo->vka_objects[i].cptr);
        }
#endif

        vka_free_object(server_vka, &mo->vka_objects[i]);
    }

    free(mo->frame_caps_in_root_task);
    free(mo->frame_paddrs);
    free(mo->vka_objects);
}