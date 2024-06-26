/**
 * @file pd_obj.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the methods to manipulate the pd object
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <sel4utils/process.h>
#include <sel4utils/vspace.h>

#include <vka/capops.h>
#include <allocman/bootstrap.h>
#include <allocman/vka.h>
#include <simple/simple_helpers.h>
#include <utils/uthash.h>
#include <cpio/cpio.h>

#include <sel4gpi/pd_component.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/mo_component.h>
#include <sel4gpi/ads_component.h>
#include <sel4gpi/cpu_component.h>
#include <sel4gpi/endpoint_component.h>
#include <sel4gpi/cap_tracking.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/ads_obj.h>
#include <sel4gpi/cpu_obj.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/resource_server_clientapi.h>
#include <sel4gpi/pd_utils.h>
#include <sel4gpi/resource_space_component.h>
#include <sel4gpi/error_handle.h>
#include <sel4gpi/linked_list.h>

#define CSPACE_SIZE_BITS 17
#define ELF_LIB_DATA_SECTION ".lib_data"
#define ELF_APP_DATA_SECTION ".data"
#define REMOTE_RR_N_PAGES 8

/* This is doesn't belong here but we need it */
extern char _cpio_archive[];
extern char _cpio_archive_end[];

// Defined for utility printing macros
#define DEBUG_ID PD_DEBUG
#define SERVER_ID PDSERVS

static int pd_setup_cspace(pd_t *pd, vka_t *vka);
static int pd_dump_internal(pd_t *pd, model_state_t *ms);

int pd_add_resource(pd_t *pd, gpi_cap_t type, uint32_t space_id, uint32_t res_id,
                    seL4_CPtr slot_in_RT, seL4_CPtr slot_in_PD, seL4_CPtr slot_in_serverPD)
{
    // Unique resource ID is the badge with the following fields: type, space_id, res_id
    uint64_t res_node_id = universal_res_id(type, space_id, res_id);
    pd_hold_node_t *node = (pd_hold_node_t *)resource_server_registry_get_by_id(&pd->hold_registry, res_node_id);

    if (node != NULL)
    {
        OSDB_PRINTF("Warning: adding resource with existing ID (%lx), do not insert again\n", res_node_id);
        BADGE_PRINT(res_node_id);
    }
    else
    {
        node = calloc(1, sizeof(pd_hold_node_t));
        node->type = type;
        node->res_id = res_id;
        node->space_id = space_id;
        node->slot_in_RT_Debug = slot_in_RT;
        node->slot_in_PD_Debug = slot_in_PD;
        node->slot_in_ServerPD_Debug = slot_in_serverPD;
        node->gen.object_id = res_node_id;

        resource_server_registry_insert(&pd->hold_registry, (resource_server_registry_node_t *)node);
    }

    return 0;
}

/**
 * Internal function to revoke a resource from a PD and remove the metadata
 */
static void pd_remove_resource_internal(pd_t *pd, resource_server_registry_node_t *hold_node)
{
    int error = 0;

    // Delete the capability
    seL4_CPtr cap = ((pd_hold_node_t *)hold_node)->slot_in_PD_Debug;

    if (cap == seL4_CapNull)
    {
        if (pd->id != get_gpi_server()->rt_pd_id)
        {
            pd_hold_node_t *node = (pd_hold_node_t *)hold_node;
            OSDB_PRINTERR("Warning: remove resource %s_%d_%d from PD, slot_in_PD is null!\n",
                          cap_type_to_str(node->type), node->space_id, node->res_id);
        }
    }
    else
    {
        /**
         * (XXX) Arya:
         * If we revoke a badged endpoint, it will delete any copies of the badged endpoint
         * Thus this will only delete copies of this resource within this PD, not any other badged endpoints
         * made from the same, original raw endpoint.
         */
        cspacepath_t path;
        vka_cspace_make_path(pd->pd_vka, cap, &path);
        error = vka_cnode_revoke(&path);
        if (error)
        {
            OSDB_PRINTERR("Failed to revoke resource from PD(%d)\n", pd->id);
        }

        error = vka_cnode_delete(&path);
        if (error)
        {
            OSDB_PRINTERR("Failed to delete resource from PD(%d)\n", pd->id);
        }

        pd->pd_vka->cspace_free(pd->pd_vka->data, cap);
    }

    // Remove the node
    resource_server_registry_delete(&pd->hold_registry, hold_node);
}

int pd_remove_resource(pd_t *pd, gpi_cap_t type, uint32_t space_id, uint32_t res_id)
{
    // See if the resource exists, remove it if so
    uint64_t res_node_id = universal_res_id(type, space_id, res_id);
    resource_server_registry_node_t *node = resource_server_registry_get_by_id(&pd->hold_registry, res_node_id);

    if (node != NULL)
    {
        pd_remove_resource_internal(pd, node);
    }

    return 0;
}

bool pd_has_resources_in_space(pd_t *pd, uint32_t space_id)
{
    // Search through the held resources, check if any belong to the given space ID
    resource_server_registry_node_t *curr, *tmp;
    HASH_ITER(hh, pd->hold_registry.head, curr, tmp)
    {
        if (((pd_hold_node_t *)curr)->space_id == space_id)
        {
            return true;
        }
    }

    return false;
}

int pd_remove_resources_in_space(pd_t *pd, uint32_t space_id)
{
    // Search through the held resources, remove any belonging to the given space ID
    resource_server_registry_node_t *curr, *tmp;
    HASH_ITER(hh, pd->hold_registry.head, curr, tmp)
    {
        if (((pd_hold_node_t *)curr)->space_id == space_id)
        {
            pd_remove_resource_internal(pd, curr);
        }
    }

    return 0;
}

static int pd_rde_find_idx(pd_t *pd,
                           gpi_cap_t type,
                           uint32_t space_id)
{
    int idx = -1;

    // search within the entries for this type and space id
    for (int i = 0; i < MAX_NS_PER_RDE; i++)
    {
        if (space_id == RESSPC_ID_NULL && pd->shared_data->rde[type][i].type.type == type)
        {
            // Just return the first entry we find if given a null space ID
            idx = i;
            break;
        }
        else if (pd->shared_data->rde[type][i].space_id == space_id)
        {
            idx = i;
            break;
        }
    }

    return idx;
}

osmosis_rde_t *pd_rde_get(pd_t *pd,
                          gpi_cap_t type,
                          uint32_t space_id)
{
    int idx = pd_rde_find_idx(pd, type, space_id);

    if (idx == -1)
    {
        return NULL;
    }
    else
    {
        return &pd->shared_data->rde[type][idx];
    }
}

void pd_add_type_name(pd_t *pd,
                      rde_type_t type,
                      char *type_name)
{
    strncpy(pd->shared_data->type_names[type.type], type_name, RESOURCE_TYPE_MAX_STRING_SIZE);
}

int pd_add_rde(pd_t *pd,
               rde_type_t type,
               char *type_name,
               uint32_t space_id,
               seL4_CPtr server_ep)
{
    // Add the RDE to the init data structure
    int idx = -1;

    for (int i = 0; i < MAX_NS_PER_RDE; i++)
    {
        osmosis_rde_t rde = pd->shared_data->rde[type.type][i];
        if (rde.space_id == space_id && rde.type.type == type.type)
        {
            // The entry already exists
            idx = -1;
            break;
        }

        if (rde.type.type == GPICAP_TYPE_NONE)
        {
            // This is an empty entry we can fill
            idx = i;
            break;
        }
    }

    if (idx == -1)
    {
        OSDB_PRINTF("Either no more RDE NS slots available for type %d or RDE being added already exists\n", type.type);
        return 1;
    }

    pd_add_type_name(pd, type, type_name);

    pd->shared_data->rde[type.type][idx].space_id = space_id;
    /* we don't really need to keep this if we index by type, but let's just keep it around for now */
    pd->shared_data->rde[type.type][idx].type = type;
    pd->shared_data->rde[type.type][idx].slot_in_RT = server_ep;

    // Badge the endpoint into the client PD
    uint32_t client_id = pd->id;

    // Badge the raw endpoint for the client PD
    cspacepath_t src, dest;
    vka_cspace_make_path(get_gpi_server()->server_vka, server_ep, &src);

    int error = vka_cspace_alloc_path(pd->pd_vka, &dest);
    if (error)
    {
        return error;
    }

    seL4_Word badge_val = gpi_new_badge(type.type,
                                        0x00,
                                        client_id,
                                        space_id,
                                        BADGE_OBJ_ID_NULL);

    error = vka_cnode_mint(&dest,
                           &src,
                           seL4_AllRights,
                           badge_val);
    if (error)
    {
        return error;
    }

    pd->shared_data->rde[type.type][idx].slot_in_PD = dest.capPtr;

    OSDB_PRINTF("Added new RDE of type %s to PD %d, in slot %d, with badge %lx\n", cap_type_to_str(type.type), client_id, (int)dest.capPtr, badge_val);

    pd->shared_data->rde_count++;
    return 0;
}

/**
 * Internal function to remove an RDE entry by type and index
 *
 * @param pd target PD
 * @param type the entry type
 * @param idx the index within the type
 */
static int pd_remove_rde_by_idx(pd_t *pd, rde_type_t type, int idx)
{
    int error = 0;

    // Revoke / delete the RDE endpoint
    // The RDE ep is badged once per client, so this will only delete this PD's copy of the endpoint
    cspacepath_t rde_ep_path;
    vka_cspace_make_path(pd->pd_vka, pd->shared_data->rde[type.type][idx].slot_in_PD, &rde_ep_path);
    error = vka_cnode_revoke(&rde_ep_path);
    SERVER_GOTO_IF_ERR(error, "Failed to revoke RDE endpoint (slot %d) for PD (%d)\n", rde_ep_path.capPtr, pd->id);
    error = vka_cnode_delete(&rde_ep_path);
    SERVER_GOTO_IF_ERR(error, "Failed to delete RDE endpoint (slot %d) for PD (%d)\n", rde_ep_path.capPtr, pd->id);

    uint64_t space_id = pd->shared_data->rde[type.type][idx].space_id;
    OSDB_PRINTF("Removed RDE of type %s, space %d from PD (%d)\n", cap_type_to_str(type.type), space_id, pd->id);

    // Clear the entry
    pd->shared_data->rde[type.type][idx].space_id = RESSPC_ID_NULL;
    pd->shared_data->rde[type.type][idx].type.type = GPICAP_TYPE_NONE;
    pd->shared_data->rde[type.type][idx].slot_in_RT = seL4_CapNull;
    pd->shared_data->rde[type.type][idx].slot_in_PD = seL4_CapNull;

    pd->shared_data->rde_count--;

err_goto:
    return error;
}

int pd_remove_rde(pd_t *pd,
                  rde_type_t type,
                  uint32_t space_id)
{
    int error = 0;

    // Find the RDE entry in the structure
    bool found_entry = false;

    assert(type.type > 0 && type.type < GPICAP_TYPE_MAX);

    for (int i = 0; i < MAX_NS_PER_RDE; i++)
    {
        osmosis_rde_t rde = pd->shared_data->rde[type.type][i];

        if (rde.type.type == type.type && (rde.space_id == space_id || space_id == RESSPC_ID_NULL))
        {
            found_entry = true;

            error = pd_remove_rde_by_idx(pd, type, i);
            SERVER_GOTO_IF_ERR(error, "Failed to remove RDE[%d][%d] from PD (%d)\n", type, i, pd->id);

            if (space_id != RESSPC_ID_NULL)
            {
                break;
            }
        }
    }

    if (found_entry == false)
    {
        // This may not be an error, just print
        OSDB_PRINTF("Could not find RDE type %d, id %d to remove\n", type.type, space_id);
        return 1;
    }

err_goto:
    return error;
}

linked_list_t *pd_get_resources_of_type(pd_t *pd, gpi_cap_t type)
{
    linked_list_t *found = linked_list_new();

    for (pd_hold_node_t *current_cap = (pd_hold_node_t *)pd->hold_registry.head; current_cap != NULL; current_cap = (pd_hold_node_t *)current_cap->gen.hh.next)
    {
        if (current_cap->type == type)
        {
            linked_list_insert(found, current_cap);
        }
    }

    return found;
}

int pd_bulk_add_resource(pd_t *pd, linked_list_t *resources)
{
    int error = 0;
    int in_error = 0;
    pd_hold_node_t *res;
    for (linked_list_node_t *curr = resources->head; curr != NULL; curr = curr->next)
    {
        res = (pd_hold_node_t *)curr->data;
        resspc_component_registry_entry_t *resource_space_data = resource_space_get_entry_by_id(res->space_id);
        seL4_CPtr copied_res = resource_server_make_badged_ep(get_pd_component()->server_vka, pd->pd_vka,
                                                              resource_space_data->space.server_ep, resource_space_data->space.resource_type,
                                                              res->space_id, res->res_id, pd->id);

        SERVER_PRINT_IF_ERR(in_error, "Warning: Could not mint resource endpoint\n");
        error = error || in_error;

        if (!in_error)
        {
            in_error = pd_add_resource(pd, res->type, res->space_id, res->res_id, res->slot_in_RT_Debug, copied_res, res->slot_in_ServerPD_Debug);
            SERVER_PRINT_IF_ERR(in_error, "Warning: failed to add resource (type: %s, space_id: %d) to PD %d\n", cap_type_to_str(res->type), res->space_id, pd->id);
            error = error || in_error;
        }
    }

    return error;
}

static void
pd_held_resource_on_delete(resource_server_registry_node_t *node_gen, void *pd_v)
{
    int error = 0;
    pd_hold_node_t *node = (pd_hold_node_t *)node_gen;
    pd_t *pd = (pd_t *)pd_v;

    OSDB_PRINTF("Freeing resource %s_%d_%d from PD (%d)\n",
                cap_type_to_str(node->type), node->space_id, node->res_id,
                pd->id);

    // If the resource is a core resource, free it directly
    // Decrement the registry entry's count, and if it reaches zero, the resource will be freed
    switch (node->type)
    {
    case GPICAP_TYPE_ADS:
        error = resource_component_dec(get_ads_component(), node->res_id);
        SERVER_GOTO_IF_ERR(error, "failed to decrement ADS resource\n");
        error = pd_component_remove_resource_from_rt(node->type, node->space_id, node->res_id);
        break;
    case GPICAP_TYPE_CPU:
        error = resource_component_dec(get_cpu_component(), node->res_id);
        SERVER_GOTO_IF_ERR(error, "failed to decrement CPU resource\n");
        error = pd_component_remove_resource_from_rt(node->type, node->space_id, node->res_id);
        break;
    case GPICAP_TYPE_MO:
        error = resource_component_dec(get_mo_component(), node->res_id);
        SERVER_GOTO_IF_ERR(error, "failed to decrement MO resource\n");
        error = pd_component_remove_resource_from_rt(node->type, node->space_id, node->res_id);
        break;
    case GPICAP_TYPE_PD:
        // (XXX) Arya: I think we do not want to destroy a PD when the refcount reaches zero
        // If it dies on its own, then it will be destroyed
        break;
    case GPICAP_TYPE_VMR:
        // NS ID is the ADS, res ID is the VMR
        error = ads_component_rm_by_id(node->space_id, node->res_id);
        break;
    case GPICAP_TYPE_RESSPC:
        // Wait to clean up resource spaces until later
        error = resspc_component_mark_delete(node->res_id);
        break;
    case GPICAP_TYPE_EP:
        error = resource_component_dec(get_ep_component(), node->res_id);
        SERVER_GOTO_IF_ERR(error, "failed to decrement EP resource\n");
        error = pd_component_remove_resource_from_rt(node->type, node->space_id, node->res_id);
        break;
    default:
        // Otherwise, call the manager PD
        resspc_component_registry_entry_t *space_data = resource_space_get_entry_by_id(node->space_id);
        SERVER_GOTO_IF_COND(space_data == NULL, "couldn't find resource space (%d)\n", node->space_id);

        // If the space is deleted,
        // or the manager PD is this PD itself,
        // then there's no point in notifying the manager
        if (space_data->space.deleted || space_data->space.pd_id == pd->id)
        {
            break;
        }

        // Find the manager PD
        pd_component_registry_entry_t *manager_pd_data = pd_component_registry_get_entry_by_id(space_data->space.pd_id);

        // Queue the "free" operation for the resource manager
        pd_work_entry_t *work_entry = calloc(1, sizeof(pd_work_entry_t));
        work_entry->res_id.type = node->type;
        work_entry->res_id.space_id = node->space_id;
        work_entry->res_id.object_id = node->res_id;

        pd_component_queue_free_work(manager_pd_data, work_entry);
        break;
    }

err_goto:
    if (error)
    {
        OSDB_PRINTERR("Warning: Could not free PD's held resource %s-%d\n", cap_type_to_str(node->type), node->res_id);
    }
}

int pd_new(pd_t *pd,
           vka_t *server_vka,
           vspace_t *server_vspace,
           mo_t *osm_data_mo)
{
    int error;
    OSDB_PRINTF("new PD: \n");

    SERVER_GOTO_IF_COND(osm_data_mo == NULL, "No MO given to hold PD's OSmosis data\n");

    pd->shared_data_mo_id = osm_data_mo->id;
    error = ads_component_attach_to_rt(osm_data_mo->id, (void **)&pd->shared_data);
    SERVER_GOTO_IF_ERR(error, "Failed to attach init data MO to RT\n");

    // Initialize the hold registry
    resource_server_initialize_registry(&pd->hold_registry, pd_held_resource_on_delete, (void *)pd);

    // Setup init data
    pd->shared_data->rde_count = 0;
    memset(pd->shared_data->rde, 0, sizeof(osmosis_rde_t) * MAX_PD_OSM_RDE);
    pd->shared_data->pd_conn.id = pd->id;

    // Setup the cspace
    error = pd_setup_cspace(pd, get_pd_component()->server_vka);
    SERVER_GOTO_IF_ERR(error, "Failed to setup PD's CSpace\n");

    pd->image_name = "PD"; // default name, since if a PD isn't a process, this never gets set

    // Allocate and badge the RT->PD notification
    error = vka_alloc_notification(server_vka, &pd->notification);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate PD's notification\n");

    cspacepath_t src, dest;
    vka_cspace_make_path(server_vka, pd->notification.cptr, &src);
    error = vka_cspace_alloc_path(server_vka, &dest);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate slot for PD's badged notification\n");

    error = vka_cnode_mint(&dest, &src, seL4_NoRead, NOTIF_BADGE);
    SERVER_GOTO_IF_ERR(error, "Failed to mint PD's badged notification\n");
    pd->badged_notification = dest.capPtr;

err_goto:
    return error;
}

void pd_destroy(pd_t *pd, vka_t *server_vka, vspace_t *server_vspace)
{
    int error = 0;
    int pd_id = pd->id;

    OSDB_PRINTF("Destroying PD (%d, %s)\n", pd_id, pd->image_name);
    pd->deleted = true;

    /* stop the PD's CPU, if not already stopped */
    error = cpu_component_stop(pd->shared_data->cpu_conn.id);
    SERVER_GOTO_IF_ERR(error, "Failed to stop CPU (%d) while destroying PD (%d)\n",
                       pd->shared_data->cpu_conn.id, pd_id);

    /* Reply with an error to any client waiting on this PD */
    if (pd->shared_data->reply_cap != seL4_CapNull)
    {
        // Copy the reply cap to the RT cspace
        cspacepath_t reply_cap_path_in_pd;
        cspacepath_t reply_cap_path_in_rt;
        vka_cspace_make_path(pd->pd_vka, pd->shared_data->reply_cap, &reply_cap_path_in_pd);
        vka_cspace_alloc_path(server_vka, &reply_cap_path_in_rt);

        // error = vka_cnode_copy(&reply_cap_path_in_rt, &reply_cap_path_in_pd, seL4_CanWrite);
        error = vka_cnode_move(&reply_cap_path_in_rt, &reply_cap_path_in_pd);
        SERVER_GOTO_IF_ERR(error, "Failed to move reply cap (%d) while destroying PD (%d)\n",
                           pd->shared_data->reply_cap,
                           pd_id);

        seL4_MessageInfo_t reply_tag = seL4_MessageInfo_new(1, 0, 0, 0);
        seL4_Send(reply_cap_path_in_rt.capPtr, reply_tag);
    }

    /* decrement the refcount of the PD's binded ADS and CPU */
    resource_component_dec(get_cpu_component(), pd->shared_data->cpu_conn.id);
    resource_component_dec(get_ads_component(), pd->shared_data->ads_conn.id);

    // This should destroy the CPU, if this is the only PD using it
    // Then the TCB is destroyed, including the internal copies of IPC frame cap and fault endpoint cap

    /* below is copied from sel4utils_destroy_process */
    /* (XXX) Arya: eventually should be repartitioned to other components */
    vka_t *vka = server_vka;

    /* destroy the cnode */
    cspacepath_t path;
    vka_cspace_make_path(vka, pd->cspace.cptr, &path);
    /* need to revoke the cnode to remove any self references that would keep the object
     * alive when we try to delete it */
    vka_cnode_revoke(&path);
    vka_free_object(vka, &pd->cspace);

    /* destroy the notification object */
    vka_cspace_make_path(vka, pd->notification.cptr, &path);
    vka_cnode_revoke(&path);
    vka_free_object(vka, &pd->notification);
    vka_cspace_free(vka, pd->badged_notification);

    /* Free elf information */
    if (pd->elf_phdrs)
    {
        free(pd->elf_phdrs);
    }

    // Hash table of holding resources
    // (XXX) Arya: This can trigger sys_munmap which is not supported
    // This also triggers resource deletion, if this PD held the last copy
    resource_server_registry_node_t *current, *tmp;
    HASH_ITER(hh, pd->hold_registry.head, current, tmp)
    {
        resource_server_registry_delete(&pd->hold_registry, current);
    }

    // free the MO for init data
    // the MO will be destroyed once all references are removed
    error = ads_component_remove_from_rt((void *)pd->shared_data);
    SERVER_GOTO_IF_ERR(error, "Failed to remove PD's init data from RT\n");

    // The PD's VKA/allocator are destroyed with allocator_mem_pool

    // Cleanup any resource space(s) this PD held
    error = resspc_component_sweep();
    SERVER_GOTO_IF_ERR(error, "Failed to sweep resource spaces after PD destroy\n");

    // Remove this PD from any other PDs that hold it
    error = pd_component_resource_cleanup(GPICAP_TYPE_PD, get_pd_component()->space_id, pd_id);
    SERVER_GOTO_IF_ERR(error, "Failed to remove destroyed PD resource from other PDs\n");

    return;

err_goto:
    OSDB_PRINTERR("Error while destroying PD (%d)\n", pd_id);
}

int pd_next_slot(pd_t *pd,
                 seL4_CPtr *next_free_slot)
{

    cspacepath_t path;
    int error = vka_cspace_alloc_path(pd->pd_vka, &path);
    *next_free_slot = error == seL4_NoError ? path.capPtr : seL4_CapNull;
    return error;
}

int pd_clear_slot(pd_t *pd,
                  seL4_CPtr slot)
{
    // Try to delete slot contents
    cspacepath_t path;
    vka_cspace_make_path(pd->pd_vka, slot, &path);
    return vka_cnode_delete(&path);
}

int pd_free_slot(pd_t *pd,
                 seL4_CPtr slot)
{
    /*
    (XXX) Arya: Can't use vka_cspace_free because it tries to identify
    the cap based on the current cspace
    // vka_cspace_free(&pd->pd_vka, slot);
    */
    pd->pd_vka->cspace_free(pd->pd_vka->data, slot);
    return 0;
}

int pd_bootstrap_allocator(pd_t *pd,
                           seL4_CPtr root,
                           size_t start_slot,
                           size_t end_slot,
                           size_t size_bits,
                           size_t guard_bits)
{
    int error;
    cspace_single_level_t *cspace = calloc(1, sizeof(cspace_single_level_t));
    allocman_t *allocator = bootstrap_create_allocman(PD_ALLOCATOR_STATIC_POOL_SIZE,
                                                      pd->allocator_mem_pool);

    error = cspace_single_level_create(allocator,
                                       cspace,
                                       (struct cspace_single_level_config){
                                           .cnode = root, .cnode_size_bits = size_bits,
                                           //.cnode_guard_bits = seL4_WordBits - pd->cspace_size_bits,
                                           .cnode_guard_bits = guard_bits,
                                           .first_slot = start_slot,
                                           .end_slot = end_slot});
    if (error != seL4_NoError)
    {
        OSDB_PRINTF("%s: Failed to initialize single-level cspace for PD id %d.\n",
                    __FUNCTION__, pd->id);
        return -1;
    }

    error = allocman_attach_cspace(allocator, cspace_single_level_make_interface(cspace));
    if (error != seL4_NoError)
    {
        OSDB_PRINTF("%s: Failed to attach cspace to allocman for PD id %d.\n",
                    __FUNCTION__, pd->id);
        return -1;
    }

    pd->pd_vka = malloc(sizeof(vka_t));
    assert(pd->pd_vka != NULL);

    allocman_make_vka(pd->pd_vka, allocator);
    return 0;
}

static int pd_setup_cspace(pd_t *pd, vka_t *vka)
{
    int error = 0;
    pd->cspace_size = CSPACE_SIZE_BITS;
    pd->cnode_guard = api_make_guard_skip_word(seL4_WordBits - pd->cspace_size);

    error = vka_alloc_cnode_object(vka, pd->cspace_size, &pd->cspace);
    SERVER_GOTO_IF_ERR(error, "Failed to create PD %d's cspace", pd->id);

    pd->shared_data->cspace_root = PD_CAP_ROOT;
    /* first slot is always 1, never allocate 0 as a cslot */
    uint32_t cspace_next_free = 1;

    /*  mint the cnode cap into the PD's cspace */
    cspacepath_t src;
    vka_cspace_make_path(vka, pd->cspace.cptr, &src);
    cspacepath_t dest = {.capPtr = cspace_next_free, .root = src.capPtr, .capDepth = pd->cspace_size};
    error = vka_cnode_mint(&dest, &src, seL4_AllRights, pd->cnode_guard);
    SERVER_GOTO_IF_ERR(error, "Failed to mint PD %d's cnode into its cspace\n");
    cspace_next_free++;

    /* Initialize a vka for the PD's cspace */
    error = pd_bootstrap_allocator(pd, pd->cspace.cptr, cspace_next_free,
                                   BIT(CSPACE_SIZE_BITS), CSPACE_SIZE_BITS, 0);
    SERVER_GOTO_IF_ERR(error, "Failed to setup allocator for PD %d\n", pd->id);

    OSDB_PRINTF("PD next free slot: %ld\n", cspace_next_free);

    return 0;

err_goto:
    if (pd->cspace.cptr != 0)
    {
        vka_free_object(vka, &pd->cspace);
    }

    return 1;
}

int pd_send_cap(pd_t *to_pd,
                seL4_CPtr cap,
                seL4_Word badge,
                seL4_Word *slot,
                bool inc_refcount,
                bool update_core_cap)
{
    int error = 0;
    SERVER_GOTO_IF_COND(badge == 0 && cap == 0, "Both badge and cap to send are NULL\n");

    gpi_cap_t cap_type = get_cap_type_from_badge(badge);
    vka_t *server_vka;
    seL4_CPtr server_src_cap;
    bool should_mint = true;

    OSDB_PRINTF("Sending %s cap to PD %d, with badge: ", cap_type_to_str(cap_type), to_pd->id);
    BADGE_PRINT(badge);

    switch (cap_type)
    {
    case GPICAP_TYPE_ADS:
        server_vka = get_ads_component()->server_vka;
        server_src_cap = get_ads_component()->server_ep;

        // Copying the resource, so increase the reference count
        if (inc_refcount)
        {
            resource_component_inc(get_ads_component(), get_object_id_from_badge(badge));
        }
        break;
    case GPICAP_TYPE_MO:
        server_vka = get_mo_component()->server_vka;
        server_src_cap = get_mo_component()->server_ep;

        // Copying the resource, so increase the reference count
        if (inc_refcount)
        {
            resource_component_inc(get_mo_component(), get_object_id_from_badge(badge));
        }
        break;
    case GPICAP_TYPE_CPU:
        server_vka = get_cpu_component()->server_vka;
        server_src_cap = get_cpu_component()->server_ep;

        // Copying the resource, so increase the reference count
        if (inc_refcount)
        {
            resource_component_inc(get_cpu_component(), get_object_id_from_badge(badge));
        }
        break;
    case GPICAP_TYPE_PD:
        server_vka = get_pd_component()->server_vka;
        server_src_cap = get_pd_component()->server_ep;

        // Copying the resource, so increase the reference count
        if (inc_refcount)
        {
            resource_component_inc(get_pd_component(), get_object_id_from_badge(badge));
        }
        break;
    case GPICAP_TYPE_EP:
        server_vka = get_ep_component()->server_vka;
        server_src_cap = get_ep_component()->server_ep;

        // Copying the resource, so increase the reference count
        if (inc_refcount)
        {
            resource_component_inc(get_ep_component(), get_object_id_from_badge(badge));
        }
        break;
    default:
        // (XXX) Linh: what happens when we send non-core GPI caps?
        should_mint = false;
        break;
    }

    if (should_mint)
    {
        seL4_CPtr new_cap = resource_server_make_badged_ep(server_vka,
                                                           to_pd->pd_vka,
                                                           server_src_cap,
                                                           cap_type,
                                                           get_space_id_from_badge(badge),
                                                           get_object_id_from_badge(badge),
                                                           to_pd->id);

        SERVER_GOTO_IF_COND(new_cap == seL4_CapNull, "Failed to mint a new cap\n");

        if (update_core_cap)
        {
            error = pd_set_core_cap(to_pd, badge, new_cap);
            SERVER_PRINT_IF_ERR(error, "Warning: failed to set the cap in PD's OSmosis data\n");
        }

        error = pd_add_resource(to_pd,
                                cap_type,
                                get_space_id_from_badge(badge),
                                get_object_id_from_badge(badge),
                                server_src_cap,
                                new_cap,
                                server_src_cap);
        SERVER_PRINT_IF_ERR(error, "Warning: Failed to add cap to PD's resources\n");

        *slot = new_cap;
    }
    else
    {
        OSDB_PRINTF("[Warning]: Untracked cap being sent to PD\n");
        cspacepath_t dest = {0};
        error = resource_server_transfer_cap(get_pd_component()->server_vka, to_pd->pd_vka, cap, &dest, false, 0);
        SERVER_GOTO_IF_ERR(error, "Failed to copy cap to PD\n");

        *slot = dest.capPtr;
    }

    OSDB_PRINTF("pd_send_cap: copied cap at %ld to child\n", *slot);

err_goto:
    return error;
}

// Add rows to model state for one resource
static int res_dump(pd_t *pd, model_state_t *ms, pd_hold_node_t *current_cap, gpi_model_node_t *pd_node)
{
    int error = 0;

    /* Check if the resource is already dumped */
    gpi_model_node_t *res_node = get_resource_node(ms, current_cap->type, current_cap->space_id, current_cap->res_id);

    switch (current_cap->type)
    {
    case GPICAP_TYPE_NONE:
        break;
    case GPICAP_TYPE_ADS:
        if (!res_node)
        {
            /* Add the resource node */
            res_node = add_resource_node(ms, current_cap->type, current_cap->space_id, current_cap->res_id);

            ads_component_registry_entry_t *ads_data = (ads_component_registry_entry_t *)
                resource_component_registry_get_by_id(get_ads_component(), current_cap->res_id);
            SERVER_GOTO_IF_COND(ads_data == NULL, "Failed to find ADS data\n");

            ads_dump_rr(&ads_data->ads, ms, pd_node);
        }

        /* Add the hold edge */
        add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, res_node);

        break;
    case GPICAP_TYPE_MO:
        if (!res_node)
        {
            /* Add the resource node */
            res_node = add_resource_node(ms, current_cap->type, current_cap->space_id, current_cap->res_id);

            mo_component_registry_entry_t *mo_data = (mo_component_registry_entry_t *)
                resource_component_registry_get_by_id(get_mo_component(), current_cap->res_id);
            SERVER_GOTO_IF_COND(mo_data == NULL, "Failed to find MO data\n");

            mo_dump_rr(&mo_data->mo, ms, pd_node);
        }

        /* Add the hold edge */
        add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, res_node);
        break;
    case GPICAP_TYPE_CPU:
        if (!res_node)
        {
            /* Add the resource node */
            res_node = add_resource_node(ms, current_cap->type, current_cap->space_id, current_cap->res_id);

            cpu_component_registry_entry_t *cpu_data = (cpu_component_registry_entry_t *)
                resource_component_registry_get_by_id(get_cpu_component(), current_cap->res_id);
            SERVER_GOTO_IF_COND(cpu_data == NULL, "Failed to find CPU data\n");

            cpu_dump_rr(&cpu_data->cpu, ms, pd_node);
        }

        /* Add the hold edge */
        add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, res_node);
        break;
    case GPICAP_TYPE_seL4:
        // Use some other method to get the cap details
        break;
    case GPICAP_TYPE_PD:
        if (!res_node && current_cap->res_id != pd->id)
        {
            /* Add the PD Node */
            pd_component_registry_entry_t *pd_data = pd_component_registry_get_entry_by_id(current_cap->res_id);
            SERVER_GOTO_IF_COND(pd_data == NULL, "Failed to find PD%d's data\n", current_cap->res_id);

            pd_dump_internal(&pd_data->pd, ms);
        }

        // Don't add a hold edge for PDs
        break;
    case GPICAP_TYPE_RESSPC:
        resspc_component_registry_entry_t *space_data = resource_space_get_entry_by_id(current_cap->res_id);
            SERVER_GOTO_IF_COND(space_data == NULL, "Failed to find resource space data\n");
        res_node = get_resource_space_node(ms, space_data->space.resource_type, space_data->space.id);

        if (!res_node)
        {
            /* Add the resource space node */
            res_node = add_resource_space_node(ms, space_data->space.resource_type, space_data->space.id);
            resspc_dump_rr(&space_data->space, ms, pd_node);
        }

        /* Add the hold edge */
        add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, res_node);
        break;
    case GPICAP_TYPE_VMR:
        // (XXX) Arya: Do not need to dump VMR, handled in ADS component
        break;
    case GPICAP_TYPE_EP:
        // Don't dump endpoints, as they aren't part of the model, and tracked only for cleanup purposes
        break;
    default:
        if (!res_node)
        {
            /* Add the resource node */
            res_node = add_resource_node(ms, current_cap->type, current_cap->space_id, current_cap->res_id);

            /* Find the resource space */
            resspc_component_registry_entry_t *space_entry = resource_space_get_entry_by_id(current_cap->space_id);
            SERVER_GOTO_IF_COND(space_entry == NULL, "Failed to find resource space (%d)\n", current_cap->space_id);

            /* Add the subset edge */
            char space_id[CSV_MAX_STRING_SIZE];
            get_resource_space_id(space_entry->space.resource_type, space_entry->space.id, &space_id);
            add_edge_by_id(ms, GPI_EDGE_TYPE_SUBSET, res_node->id, space_id);

            /* Find the resource server */
            pd_component_registry_entry_t *manager_pd_entry =
                pd_component_registry_get_entry_by_id(space_entry->space.pd_id);
            SERVER_GOTO_IF_COND(manager_pd_entry == NULL, "Failed to find PD (%d)\n", space_entry->space.pd_id);

            /* Request additional relations for the resource */
            pd_work_entry_t *work_node = calloc(1, sizeof(gpi_res_id_t));
            work_node->res_id.type = current_cap->type;
            work_node->res_id.space_id = current_cap->space_id;
            work_node->res_id.object_id = current_cap->res_id;
            work_node->client_pd_id = pd->id;

            pd_component_queue_model_extraction_work(manager_pd_entry, work_node);
        }

        /* Add the hold edge */
        add_edge(ms, GPI_EDGE_TYPE_HOLD, pd_node, res_node);
        break;
    }

err_goto:
    return error;
}

/**
 * Dump the given PD to the given model state
 */
static int pd_dump_internal(pd_t *pd, model_state_t *ms)
{
    int error;

    OSDB_PRINTF("Extracting state of PD (%d)\n", pd->id);

    /* Check if the PD is already dumped */
    gpi_model_node_t *pd_node = get_pd_node(ms, pd->id);
    if (pd_node)
    {
        return 0; // This PD is already dumped
    }

    // Don't add the PD resource space, it is just an implementation detail but not part of the model

    /* Add the PD node */
    pd_node = add_pd_node(ms, pd->image_name, pd->id);

    /* Add request edges for all RDEs from this PD */
    for (int i = 0; i < GPICAP_TYPE_MAX; i++)
    {
        for (int j = 0; j < MAX_NS_PER_RDE; j++)
        {
            osmosis_rde_t rde = pd->shared_data->rde[i][j];

            if (rde.type.type != GPICAP_TYPE_NONE)
            {
                resspc_component_registry_entry_t *rm = resource_space_get_entry_by_id(rde.space_id);
                SERVER_GOTO_IF_COND(rm == NULL, "Couldn't find resource space (%d)\n", rde.space_id);

                /* Add the resource server PD node */
                char resource_manager_pd_id[CSV_MAX_STRING_SIZE];
                get_pd_id(rm->space.pd_id, &resource_manager_pd_id);
                add_request_edge_by_id(ms, pd_node->id, resource_manager_pd_id, rde.type.type);

                /* Request info about the space */
                if (rm->space.pd_id != get_gpi_server()->rt_pd_id)
                {
                    /* Find the resource server PD */
                    pd_component_registry_entry_t *manager_pd_entry =
                        pd_component_registry_get_entry_by_id(rm->space.pd_id);
                    SERVER_GOTO_IF_COND(rm == NULL, "Couldn't find PD (%d)\n", rde.space_id);

                    /* Request info about the resource space */
                    pd_work_entry_t *work_node = calloc(1, sizeof(gpi_res_id_t));
                    work_node->res_id.type = rde.type.type;
                    work_node->res_id.space_id = rde.space_id;
                    work_node->res_id.object_id = BADGE_OBJ_ID_NULL;
                    work_node->client_pd_id = pd->id;

                    pd_component_queue_model_extraction_work(manager_pd_entry, work_node);
                }
            }
        }
    }

    /* add caps that this PD has access to */
    for (pd_hold_node_t *current_cap = (pd_hold_node_t *)pd->hold_registry.head; current_cap != NULL; current_cap = (pd_hold_node_t *)current_cap->gen.hh.next)
    {
        // print_pd_osm_cap_info(current_cap);
        if (res_dump(pd, ms, current_cap, pd_node) != 0)
        {
            return 1;
        }
    }

err_goto:
    return error;
}

int pd_dump(pd_t *pd, model_state_t *ms)
{
    int error = 0;

    OSDB_PRINTF("pd_dump_cap: Dumping all details of PD:%u\n", pd->id);

    /*
        For all caps that belong to this PD
            switch {
                case: seL4:
                    Print Debug Info
                case: OSmosis:
                    Get the RR for that cap
            }
    */

    /* Add a special node for the RT */
    gpi_model_node_t *rt_node = get_root_node(ms);

    /* Add caps from RT (not all caps, just specially tracked ones) */
    pd_component_registry_entry_t *rt_entry = pd_component_registry_get_entry_by_id(get_gpi_server()->rt_pd_id);

    assert(rt_entry != NULL);
    pd_t *rt_pd = &rt_entry->pd;

    for (pd_hold_node_t *current_cap = (pd_hold_node_t *)rt_pd->hold_registry.head; current_cap != NULL; current_cap = (pd_hold_node_t *)current_cap->gen.hh.next)
    {
        // print_pd_osm_cap_info(current_cap);
        if (res_dump(rt_pd, ms, current_cap, rt_node) != 0)
        {
            return 1;
        }
    }

    /* Add the PD's data */
    error = pd_dump_internal(pd, ms);
    SERVER_GOTO_IF_ERR(error, "Failed PD dump\n");

#if 0
    /* Print RDE Info*/
    for (int i = 0; i < GPICAP_TYPE_MAX; i++)
    {
        for (int j = 0; j < MAX_NS_PER_RDE; j++)
        {
            print_pd_osm_rde_info(&pd->shared_data->rde[i][j]);
        }
    }
#endif

err_goto:
    return error;
}

inline void print_pd_osm_cap_info(pd_hold_node_t *o)
{
    printf("Resource_ID: %d Slot_RT:%lx\t Slot_PD: %lx\t Slot_ServerPD: %lx\t T: %s\n",
           o->res_id,
           o->slot_in_RT_Debug,
           o->slot_in_PD_Debug,
           o->slot_in_ServerPD_Debug,
           cap_type_to_str(o->type));
}

inline void print_pd_osm_rde_info(osmosis_rde_t *o)
{
    if (o)
    {
        printf("RDE: Slot_RT:%lu\t Slot_PD: %lu\t T: %s\t SpaceID: %u\n",
               o->slot_in_RT,
               o->slot_in_PD,
               cap_type_to_str(o->type.type),
               o->space_id);
    }
}

void pd_debug_print_held(pd_t *pd)
{
    for (pd_hold_node_t *current_cap = (pd_hold_node_t *)pd->hold_registry.head; current_cap != NULL; current_cap = (pd_hold_node_t *)current_cap->gen.hh.next)
    {
        print_pd_osm_cap_info(current_cap);
    }
}

inline void pd_set_image_name(pd_t *pd, const char *image_name)
{
    pd->image_name = malloc(strlen(image_name) + 1);
    strcpy(pd->image_name, image_name);
}

int pd_set_core_cap(pd_t *pd, seL4_Word core_cap_badge, seL4_CPtr core_cap)
{
    int error = 0;
    uint64_t cap_type = get_cap_type_from_badge(core_cap_badge);
    uint64_t cap_id = get_object_id_from_badge(core_cap_badge);
    OSDB_PRINTF("Setting PD%d's OSmosis %s cap\n", pd->id, cap_type_to_str(cap_type));

    switch (cap_type)
    {
    case GPICAP_TYPE_ADS:
        resource_component_inc(get_ads_component(), cap_id);
        pd->shared_data->ads_conn.id = cap_id;
        pd->shared_data->ads_conn.badged_server_ep_cspath.capPtr = core_cap;
        break;
    case GPICAP_TYPE_PD:
        pd->shared_data->pd_conn.id = cap_id;
        pd->shared_data->pd_conn.badged_server_ep_cspath.capPtr = core_cap;
        break;
    case GPICAP_TYPE_CPU:
        resource_component_inc(get_cpu_component(), cap_id);
        pd->shared_data->cpu_conn.id = cap_id;
        pd->shared_data->cpu_conn.badged_server_ep_cspath.capPtr = core_cap;
        break;
    default:
        SERVER_GOTO_IF_COND(1, "Trying to set PD OSmosis data with invalid cap\n");
        break;
    }

err_goto:
    return error;
}

void pd_make_path(pd_t *pd, seL4_CPtr cap, cspacepath_t *path)
{
    vka_cspace_make_path(pd->pd_vka, cap, path);
}
