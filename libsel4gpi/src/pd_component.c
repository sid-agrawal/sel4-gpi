/**
 * @file pd_component.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the pd server API from pd_component.h.
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */

#include <autoconf.h>

#include <stdio.h>
#include <string.h>

#include <sel4/sel4.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vka/capops.h>

#include <utils/arith.h>
#include <utils/ansi.h>
#include <sel4utils/api.h>
#include <sel4utils/strerror.h>

#include <sel4gpi/pd_clientapi.h>
#include <sel4gpi/pd_component.h>
#include <sel4gpi/pd_obj.h>
#include <sel4gpi/test_init_data.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/gpi_client.h>
#include <sel4gpi/error_handle.h>

// Defined for utility printing macros
#define DEBUG_ID PD_DEBUG
#define SERVER_ID PDSERVS

pd_component_context_t *get_pd_component(void)
{
    return &get_gpi_server()->pd_component;
}

static inline seL4_MessageInfo_t recv(seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(get_pd_component()->server_ep_obj.cptr,
                    sender_badge_ptr,
                    get_pd_component()->server_thread.reply.cptr);
}

static inline void reply(seL4_MessageInfo_t tag)
{
    api_reply(get_pd_component()->server_thread.reply.cptr, tag);
}

pd_component_registry_entry_t *pd_component_registry_get_entry_by_id(seL4_Word object_id)
{
    return (pd_component_registry_entry_t *)resource_server_registry_get_by_id(&get_pd_component()->pd_registry, object_id);
}

pd_component_registry_entry_t *pd_component_registry_get_entry_by_badge(seL4_Word badge)
{
    return (pd_component_registry_entry_t *)resource_server_registry_get_by_badge(&get_pd_component()->pd_registry, badge);
}

pd_component_resource_manager_entry_t *pd_component_resource_manager_get_entry_by_id(seL4_Word manager_id)
{
    return (pd_component_resource_manager_entry_t *)resource_server_registry_get_by_id(&get_pd_component()->server_registry, manager_id);
}

int pd_component_resource_manager_insert(pd_component_resource_manager_entry_t *new_node)
{
    resource_server_registry_insert_new_id(&get_pd_component()->server_registry, (resource_server_registry_node_t *)new_node);

    return new_node->gen.object_id;
}

// Called when an item from the CPU registry is deleted
static void on_pd_registry_delete(resource_server_registry_node_t *node_gen)
{
    pd_component_registry_entry_t *node = (pd_component_registry_entry_t *)node_gen;

    OSDB_PRINTF("Destroying PD(%d, %s)\n", node->pd.id, node->pd.image_name);

    pd_destroy(&node->pd, get_pd_component()->server_vka, get_pd_component()->server_vspace);
}

int pd_component_initialize(simple_t *server_simple,
                            vka_t *server_vka,
                            seL4_CPtr server_cspace,
                            vspace_t *server_vspace,
                            sel4utils_thread_t server_thread,
                            vka_object_t server_ep_obj)
{
    pd_component_context_t *component = get_pd_component();

    component->server_simple = server_simple;
    component->server_vka = server_vka;
    component->server_cspace = server_cspace;
    component->server_vspace = server_vspace;
    component->server_thread = server_thread;
    component->server_ep_obj = server_ep_obj;
    resource_server_initialize_registry(&component->pd_registry, on_pd_registry_delete);
    resource_server_initialize_registry(&component->server_registry, NULL);
}

// Utility function to create a PD, add to registry, badge an endpoint, etc.
static int pd_component_allocate_pd(uint64_t client_id, bool forge, pd_component_registry_entry_t **ret_entry, seL4_CPtr *ret_cap)
{
    int error = 0;

    /* Create the registry entry */
    pd_component_registry_entry_t *client_reg_ptr = malloc(sizeof(pd_component_registry_entry_t));
    SERVER_GOTO_IF_COND(client_reg_ptr == NULL, "Failed to allocate new badge for client.\n");

    memset((void *)client_reg_ptr, 0, sizeof(pd_component_registry_entry_t));

    client_reg_ptr->pd.id = resource_server_registry_insert_new_id(&get_pd_component()->pd_registry, (resource_server_registry_node_t *)client_reg_ptr);
    *ret_entry = client_reg_ptr;

    /* Create the PD object */
    if (!forge)
    {
        error = pd_new(&client_reg_ptr->pd,
                       get_pd_component()->server_vka,
                       get_pd_component()->server_vspace);

        SERVER_GOTO_IF_ERR(error, "Failed to create new pd object\n");
    }

    /* Create the badged endpoint */
    *ret_cap = resource_server_make_badged_ep(get_pd_component()->server_vka, NULL, get_pd_component()->server_ep_obj.cptr,
                                              client_reg_ptr->pd.id, GPICAP_TYPE_PD, NSID_DEFAULT, client_id);
    client_reg_ptr->pd.pd_cap_in_RT = *ret_cap;

    SERVER_GOTO_IF_COND(ret_cap == seL4_CapNull, "Failed to make badged ep for new PD\n");

    /* Add the resource to the client */
    error = pd_add_resource_by_id(client_id, GPICAP_TYPE_PD, client_reg_ptr->pd.id, NSID_DEFAULT, *ret_cap, seL4_CapNull, *ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to initialize add PD resource to PD\n");

err_goto:
    return error;
}

void forge_pd_for_root_task(uint64_t *rt_id)
{
    pd_component_registry_entry_t *rt_entry = malloc(sizeof(pd_component_registry_entry_t));
    *rt_id = resource_server_registry_insert_new_id(&get_pd_component()->pd_registry, (resource_server_registry_node_t *)rt_entry);
}

// (XXX) Arya: hack to store the test PD ID for destroying it later
uint64_t test_pd_id;

void forge_pd_cap_from_init_data(test_init_data_t *init_data, sel4utils_process_t *test_process, void **osm_init_data)
{
    assert(init_data != NULL);

    int error = 0;
    seL4_CPtr ret_cap;
    pd_component_registry_entry_t *new_entry;

    /* Allocate the PD object */
    error = pd_component_allocate_pd(get_gpi_server()->rt_pd_id, true, &new_entry, &ret_cap);
    ZF_LOGF_IFERR(error, "Failed to allocate PD for forging");
    assert(error == 0);
    pd_t *pd = &new_entry->pd;
    test_pd_id = pd->id;

    /* Update the PD object from init data */
    pd_new(pd,
           get_pd_component()->server_vka,
           get_pd_component()->server_vspace);

    // Split the test process' cspace and initialize a vka with half
    seL4_CPtr mid_slot = DIV_ROUND_UP(init_data->free_slots.start + init_data->free_slots.end, 2);
    error = pd_bootstrap_allocator(pd, test_process->cspace.cptr,
                                   mid_slot, init_data->free_slots.end,
                                   init_data->cspace_size_bits,
                                   // seL4_WordBits - init_data->cspace_size_bits);
                                   0);
    ZF_LOGF_IFERR(error, "Failed to initialize PD VKA");
    init_data->free_slots.end = mid_slot - 1;

    // Add the basic RDEs
    rde_type_t ads_type = {.type = GPICAP_TYPE_ADS};
    pd_add_rde(pd, ads_type, get_gpi_server()->ads_manager_id, NSID_DEFAULT, get_gpi_server()->server_ep_obj.cptr);

    rde_type_t cpu_type = {.type = GPICAP_TYPE_CPU};
    pd_add_rde(pd, cpu_type, get_gpi_server()->cpu_manager_id, NSID_DEFAULT, get_gpi_server()->server_ep_obj.cptr);

    rde_type_t mo_type = {.type = GPICAP_TYPE_MO};
    pd_add_rde(pd, mo_type, get_gpi_server()->mo_manager_id, NSID_DEFAULT, get_gpi_server()->server_ep_obj.cptr);

    rde_type_t pd_type = {.type = GPICAP_TYPE_PD};
    pd_add_rde(pd, pd_type, get_gpi_server()->pd_manager_id, NSID_DEFAULT, get_gpi_server()->server_ep_obj.cptr);

    // Forge ADS cap
    seL4_CPtr child_as_cap_in_parent;
    uint32_t ads_id;
    error = forge_ads_cap_from_vspace(&test_process->vspace, get_pd_component()->server_vka, pd->id, &child_as_cap_in_parent, &ads_id);
    ZF_LOGF_IFERR(error, "Failed to forge child's as cap");
    pd->init_data->binded_ads_ns_id = ads_id;

    // Forge CPU cap
    seL4_CPtr child_cpu_cap_in_parent;
    uint32_t cpu_id;
    error = forge_cpu_cap_from_tcb(test_process, get_pd_component()->server_vka, pd->id, &child_cpu_cap_in_parent, &cpu_id);
    ZF_LOGF_IFERR(error, "Failed to forge child's CPU cap");

    // Copy the ADS/CPU/PD caps to the test process
    // The refcount of each is 1
    error = copy_cap_to_pd(pd, child_as_cap_in_parent, &pd->init_data->ads_cap);
    assert(error == 0);
    pd_add_resource(pd, GPICAP_TYPE_ADS, ads_id, NSID_DEFAULT, child_as_cap_in_parent, pd->init_data->ads_cap, child_as_cap_in_parent);

    error = copy_cap_to_pd(pd, pd->pd_cap_in_RT, &pd->init_data->pd_cap);
    assert(error == 0);
    pd_add_resource(pd, GPICAP_TYPE_PD, pd->id, NSID_DEFAULT, pd->pd_cap_in_RT, pd->init_data->pd_cap, pd->pd_cap_in_RT);

    error = copy_cap_to_pd(pd, child_cpu_cap_in_parent, &pd->init_data->cpu_cap);
    assert(error == 0);
    pd_add_resource(pd, GPICAP_TYPE_CPU, cpu_id, NSID_DEFAULT, child_cpu_cap_in_parent, pd->init_data->cpu_cap, child_cpu_cap_in_parent);

    // Attach the init data to test PD
    void *init_data_vaddr = (void *)0x50000000;
    error = ads_component_attach(ads_id, pd->init_data_mo_id, SEL4UTILS_RES_TYPE_GENERIC, init_data_vaddr, &init_data_vaddr);
    assert(error == 0);

    *osm_init_data = init_data_vaddr;
    pd->init_data_in_PD = init_data_vaddr;
    OSDB_PRINTF("Test process init data is at %p\n", pd->init_data_in_PD);
}

void destroy_test_pd(void)
{
    int error = 0;

    pd_component_registry_entry_t *client_pd_data = pd_component_registry_get_entry_by_id(test_pd_id);
    SERVER_GOTO_IF_COND(client_pd_data == NULL, "Couldn't find test PD (%ld) to destroy it\n", test_pd_id);

    /* Remove the PD from registry, this will also destroy the PD */
    resource_server_registry_delete(&get_pd_component()->pd_registry, (resource_server_registry_node_t *)client_pd_data);

    return;

err_goto:
    ZF_LOGF("Failed to cleanup test PD\n");
}

int pd_add_resource_by_id(uint32_t pd_id, gpi_cap_t cap_type,
                          uint32_t res_id, uint32_t ns_id,
                          seL4_CPtr slot_in_RT,
                          seL4_CPtr slot_in_PD,
                          seL4_CPtr slot_in_serverPD)
{
    int error = 0;

    // (XXX) Arya: Why are we treating the test process specially? Can we remove this?
    if (pd_id != 0) // only test processes would have no client ID
    {
        pd_component_registry_entry_t *client_pd_data = pd_component_registry_get_entry_by_id(pd_id);
        SERVER_GOTO_IF_COND(client_pd_data == NULL, "Couldn't find PD (%d) to add resource \n", pd_id);

        error = pd_add_resource(&client_pd_data->pd, cap_type, res_id, ns_id, slot_in_RT, slot_in_PD, slot_in_serverPD);
    }

err_goto:
    return error;
}

void pd_handle_allocation_request(seL4_Word sender_badge, seL4_MessageInfo_t *reply_tag)
{
    OSDB_PRINTF("Got connect request from badge %lx\n", sender_badge);
    int error = 0;
    seL4_CPtr ret_cap;
    pd_component_registry_entry_t *new_entry;
    uint32_t client_id = get_client_id_from_badge(sender_badge);

    error = pd_component_allocate_pd(client_id, false, &new_entry, &ret_cap);

    if (error == 0)
    {
        OSDB_PRINTF("Successfully allocated a new PD.\n");
    }

    /* Return this badged end point in the return message. */
    seL4_SetCap(0, ret_cap);
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_CONNECT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, PDMSGREG_CONNECT_ACK_END);
    return reply(tag);
}

static void handle_disconnect_req(seL4_Word sender_badge,
                                  seL4_MessageInfo_t old_tag,
                                  seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got disconnect request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(sender_badge));

    uint32_t pd_id = client_data->pd.id;

    /* Remove the PD from registry, this will also destroy the PD */
    resource_server_registry_delete(&get_pd_component()->pd_registry, (resource_server_registry_node_t *)client_data);

    // (XXX) Arya: Should we be deleting from registry or just decrementing?

    OSDB_PRINTF("Cleaned up PD %d.\n", pd_id);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_DISCONNECT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, PDMSGREG_DISCONNECT_ACK_END);
    return reply(tag);
}

static void handle_next_slot_req(seL4_Word sender_badge,
                                 seL4_MessageInfo_t old_tag,
                                 seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got next slot request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(sender_badge));

    seL4_Word slot;
    error = pd_next_slot(&client_data->pd,
                         &slot);

    seL4_SetMR(PDMSGREG_NEXT_SLOT_PD_SLOT, slot);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_NEXT_SLOT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_NEXT_SLOT_ACK_END);
    return reply(tag);
}

static void handle_free_slot_req(seL4_Word sender_badge,
                                 seL4_MessageInfo_t old_tag,
                                 seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got free slot request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(sender_badge));

    seL4_Word slot = seL4_GetMR(PDMSGREG_FREE_SLOT_REQ_SLOT);
    OSDB_PRINTF("Freeing PD's slot %d.\n", (int)slot);

    error = pd_free_slot(&client_data->pd, slot);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_FREE_SLOT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_FREE_SLOT_ACK_END);
    return reply(tag);
}

static void handle_alloc_ep_req(seL4_Word sender_badge,
                                seL4_MessageInfo_t old_tag,
                                seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got alloc ep request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Failed to find client badge %lx.\n", sender_badge);

    seL4_CPtr slot;
    error = pd_alloc_ep(&client_data->pd,
                        get_pd_component()->server_vka,
                        &slot);

    seL4_SetMR(PDMSGREG_ALLOC_EP_PD_SLOT, slot);
    OSDB_PRINTF("Allocated ep in slot %d\n", (int)slot);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_ALLOC_EP_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_ALLOC_EP_ACK_END);
    return reply(tag);
}

static void handle_badge_ep_req(seL4_Word sender_badge,
                                seL4_MessageInfo_t old_tag,
                                seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got badge ep request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(sender_badge));

    seL4_Word badge = seL4_GetMR(PDMSGREG_BADGE_EP_REQ_BADGE);
    seL4_CPtr src_ep_slot = seL4_GetMR(PDMSGREG_BADGE_EP_REQ_SRC);
    seL4_Word slot;

    error = pd_badge_ep(&client_data->pd,
                        src_ep_slot,
                        badge,
                        &slot);

    seL4_SetMR(PDMSGREG_BADGE_EP_PD_SLOT, slot);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_BADGE_EP_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_BADGE_EP_ACK_END);
    return reply(tag);
}

static void handle_send_cap_req(seL4_Word sender_badge,
                                seL4_MessageInfo_t old_tag,
                                seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got send-cap request from client badge %lx.\n", sender_badge);
    int error = 0;

    /* This only works if the extra cap is a GPI core cap (badged version of GPI server EP) */
    OSDB_PRINTF("received_cap: %lu (badge: %lx)\n", received_cap, seL4_GetBadge(0));
    OSDB_PRINTF("Unwrapped: %s\n",
                seL4_MessageInfo_get_capsUnwrapped(old_tag) ? "true" : "false");

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(sender_badge));

    seL4_Word received_caps_badge = seL4_GetBadge(0);

    seL4_Word slot;
    error = pd_send_cap(&client_data->pd,
                        received_cap,
                        received_caps_badge,
                        &slot,
                        true);

    seL4_SetMR(PDMSGREG_SEND_CAP_PD_SLOT, slot);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SENDCAP_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_SEND_CAP_ACK_END);
    return reply(tag);
}

static void handle_dump_cap_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got dump-cap request from client badge %lx.\n", sender_badge);
    int error = 0;

    assert(seL4_MessageInfo_get_extraCaps(old_tag) == 0);
    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    /* Find the client */
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find PD (%ld)\n", get_object_id_from_badge(sender_badge));

    error = pd_dump(&client_data->pd);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_DUMP_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_DUMP_ACK_END);
    return reply(tag);
}

static void handle_add_rde_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    OSDB_PRINTF("add_rde_req: Got request from client badge %lx.\n", sender_badge);
    int error = 0;

    seL4_Word server_badge = seL4_GetBadge(0);
    seL4_Word manager_id = seL4_GetMR(PDMSGREG_ADD_RDE_REQ_MANAGER_ID);
    seL4_Word ns_id = seL4_GetMR(PDMSGREG_ADD_RDE_REQ_NSID);
    pd_component_registry_entry_t *target_data = pd_component_registry_get_entry_by_badge(sender_badge);
    pd_component_registry_entry_t *server_data = pd_component_registry_get_entry_by_badge(server_badge);
    pd_component_resource_manager_entry_t *resource_manager_data = pd_component_resource_manager_get_entry_by_id(manager_id);

    SERVER_GOTO_IF_COND(target_data == NULL, "Couldn't find target PD (%ld)\n", get_object_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(server_data == NULL, "Couldn't find server PD (%ld)\n", get_object_id_from_badge(server_badge));
    SERVER_GOTO_IF_COND(resource_manager_data == NULL, "Couldn't find resource_manager_data (%ld)\n", manager_id);
    SERVER_GOTO_IF_COND(server_data->pd.id != resource_manager_data->pd->id,
                        "add_rde_req: wrong server PD (%d) provided for resource manager in PD (%d)\n",
                        server_data->pd.id,
                        resource_manager_data->pd->id);

    rde_type_t rde_type = {.type = resource_manager_data->resource_type};
    error = pd_add_rde(&target_data->pd,
                       rde_type,
                       resource_manager_data->gen.object_id,
                       ns_id,
                       resource_manager_data->server_ep);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_ADD_RDE_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_ADD_RDE_ACK_END);
    return reply(tag);
}

static void handle_share_rde_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    int error = 0;

    seL4_Word type = seL4_GetMR(PDMSGREG_SHARE_RDE_REQ_TYPE);
    seL4_Word ns_id = seL4_GetMR(PDMSGREG_SHARE_RDE_REQ_NS);

    OSDB_PRINTF("share_rde_req: Got request from client badge %lx for RDE type %ld with NS %ld.\n",
                sender_badge, type, ns_id);

    seL4_Word client_id = get_client_id_from_badge(sender_badge);
    pd_component_registry_entry_t *target_data = pd_component_registry_get_entry_by_badge(sender_badge);
    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_id(client_id);

    SERVER_GOTO_IF_COND(target_data == NULL, "Couldn't find target PD (%ld)\n", get_object_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find client PD (%ld)\n", client_id);

    osmosis_rde_t *rde = pd_rde_get(&client_data->pd, type, ns_id);
    SERVER_GOTO_IF_COND(rde == NULL, "share_rde_req: Failed to find RDE for type %ld and NS_ID %ld.\n", type, ns_id);

    pd_component_resource_manager_entry_t *resource_manager_data = pd_component_resource_manager_get_entry_by_id(rde->manager_id);
    SERVER_GOTO_IF_COND(resource_manager_data == NULL, "share_rde_req: Failed to find resource manager ID %d.\n", rde->manager_id);

    rde_type_t rde_type = {.type = type};
    error = pd_add_rde(&target_data->pd,
                       rde_type,
                       resource_manager_data->gen.object_id,
                       ns_id,
                       resource_manager_data->server_ep);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_SHARE_RDE_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_SHARE_RDE_ACK_END);
    return reply(tag);
}

static void handle_register_resource_manager_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got register server request from client badge %lx.\n", sender_badge);
    int error = 0;

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find target PD (%ld)\n", get_object_id_from_badge(sender_badge));

    assert(seL4_MessageInfo_get_extraCaps(old_tag) == 1);

    pd_component_resource_manager_entry_t *rs_entry = malloc(sizeof(pd_component_resource_manager_entry_t));
    rs_entry->pd = &client_data->pd;
    rs_entry->server_ep = received_cap;
    rs_entry->resource_type = seL4_GetMR(PDMSGREG_REGISTER_SERV_REQ_TYPE);
    rs_entry->ns_index = NSID_DEFAULT;

    int manager_id = pd_component_resource_manager_insert(rs_entry);
    OSDB_PRINTF("Registered server, cap is at %ld.\n", rs_entry->server_ep);

    seL4_SetMR(PDMSGREG_REGISTER_SERV_ACK_ID, manager_id);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_REGISTER_SERV_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_REGISTER_SERV_ACK_END);
    return reply(tag);
}

static void handle_register_namespace_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got register namespace request from client badge %lx.\n", sender_badge);
    int error = 0;

    seL4_Word manager_id = seL4_GetMR(PDMSGREG_REGISTER_NS_REQ_MANAGER_ID);
    seL4_Word target_id = seL4_GetMR(PDMSGREG_REGISTER_NS_REQ_CLIENT_ID);

    pd_component_registry_entry_t *client_data = pd_component_registry_get_entry_by_badge(sender_badge);
    pd_component_registry_entry_t *target_data = pd_component_registry_get_entry_by_id(target_id);
    pd_component_resource_manager_entry_t *resource_manager_data = pd_component_resource_manager_get_entry_by_id(manager_id);

    SERVER_GOTO_IF_COND(client_data == NULL, "Couldn't find client PD (%ld)\n", get_object_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(target_data == NULL, "Couldn't find target PD (%ld)\n", target_id);
    SERVER_GOTO_IF_COND(resource_manager_data == NULL, "Couldn't find resource manager (%ld)\n", manager_id);
    SERVER_GOTO_IF_COND(resource_manager_data->pd->id != get_client_id_from_badge(sender_badge),
                        "resource manager PD (%d) and client PD (%ld) do not match.\n",
                        resource_manager_data->pd->id, get_client_id_from_badge(sender_badge));

    resource_manager_data->ns_index++;
    uint64_t ns_id = resource_manager_data->ns_index;

    // Add the RDE for the NS to the target PD
    rde_type_t rde_type = {.type = resource_manager_data->resource_type};
    pd_add_rde(&target_data->pd, rde_type, manager_id,
               ns_id, resource_manager_data->server_ep);

    OSDB_PRINTF("Registered namespace, ID is %ld.\n", ns_id);
    seL4_SetMR(PDMSGREG_REGISTER_NS_ACK_NSID, ns_id);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_REGISTER_NS_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_REGISTER_NS_ACK_END);
    return reply(tag);
}

static void handle_create_resource_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got create resource request from client badge %lx.\n", sender_badge);
    int error = 0;

    seL4_Word server_id = get_object_id_from_badge(sender_badge);
    seL4_Word manager_id = seL4_GetMR(PDMSGREG_CREATE_RES_REQ_MANAGER_ID);
    seL4_Word resource_id = get_global_object_id_from_local(manager_id, seL4_GetMR(PDMSGREG_CREATE_RES_REQ_RES_ID));

    pd_component_registry_entry_t *server_data = pd_component_registry_get_entry_by_id(server_id);
    pd_component_resource_manager_entry_t *resource_manager_data = pd_component_resource_manager_get_entry_by_id(manager_id);
    SERVER_GOTO_IF_COND(server_data == NULL, "Couldn't find client PD (%ld)\n", get_object_id_from_badge(sender_badge));
    SERVER_GOTO_IF_COND(resource_manager_data == NULL, "Couldn't find resource manager (%ld)\n", manager_id);

    gpi_cap_t resource_type = resource_manager_data->resource_type;

    OSDB_PRINTF("resource manager %ld creates resource with ID %ld\n",
                manager_id, resource_id);

    error = pd_add_resource(&server_data->pd, resource_type, resource_id, NSID_DEFAULT, seL4_CapNull, seL4_CapNull, seL4_CapNull);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_CREATE_RES_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_CREATE_RES_ACK_END);
    return reply(tag);
}

static void handle_give_resource_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got give resource request from client badge %lx, resource ID %ld.\n",
                sender_badge, seL4_GetMR(PDMSGREG_GIVE_RES_REQ_RES_ID));
    int error = 0;

    seL4_Word server_id = get_object_id_from_badge(sender_badge);
    seL4_Word recipient_id = seL4_GetMR(PDMSGREG_GIVE_RES_REQ_CLIENT_ID);
    seL4_Word manager_id = seL4_GetMR(PDMSGREG_GIVE_RES_REQ_MANAGER_ID);
    seL4_Word ns_id = seL4_GetMR(PDMSGREG_GIVE_RES_REQ_NS_ID);
    seL4_Word resource_id = get_global_object_id_from_local(manager_id, seL4_GetMR(PDMSGREG_GIVE_RES_REQ_RES_ID));
    pd_component_registry_entry_t *server_data = pd_component_registry_get_entry_by_id(server_id);
    pd_component_registry_entry_t *recipient_data = pd_component_registry_get_entry_by_id(recipient_id);
    pd_component_resource_manager_entry_t *resource_manager_data = pd_component_resource_manager_get_entry_by_id(manager_id);

    SERVER_GOTO_IF_COND(server_data == NULL, "Couldn't find server PD (%ld)\n", server_id);
    SERVER_GOTO_IF_COND(recipient_data == NULL, "Couldn't find target PD (%ld)\n", recipient_id);
    SERVER_GOTO_IF_COND(resource_manager_data == NULL, "Couldn't find resource manager (%ld)\n", manager_id);

    uint64_t res_node_id = gpi_new_badge(resource_manager_data->resource_type, 0, 0, ns_id, resource_id);
    pd_hold_node_t *resource_data = (pd_hold_node_t *)resource_server_registry_get_by_id(&server_data->pd.hold_registry, res_node_id);
    SERVER_GOTO_IF_COND(resource_data == NULL, "Couldn't find resource (%lx)\n", res_node_id);

    OSDB_PRINTF("resource manager %ld gives resource ID %ld to client %ld\n",
                manager_id, resource_id, recipient_id);

    /* Create a new badged EP for the resource */
    seL4_CPtr dest = resource_server_make_badged_ep(get_pd_component()->server_vka, &recipient_data->pd.pd_vka,
                                                    resource_manager_data->server_ep, resource_id,
                                                    resource_manager_data->resource_type, ns_id, recipient_id);
    seL4_SetMR(PDMSGREG_GIVE_RES_ACK_DEST, dest);

    // Add the resource to the PD object
    // (XXX) Arya: How to handle duplicate entries to the same resource?
    // The hash table is keyed by resource ID
    error = pd_add_resource(&recipient_data->pd, resource_manager_data->resource_type, resource_id, ns_id,
                            seL4_CapNull, dest, seL4_CapNull);
    SERVER_GOTO_IF_ERR(error, "Failed to add resource to PD (%ld)\n", recipient_id);

err_goto:
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_GIVE_RES_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0,
                                                  PDMSGREG_GIVE_RES_ACK_END);
    return reply(tag);
}

static void handle_exit_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    OSDB_PRINTF("Got exit request from client badge %lx\n", sender_badge);

    handle_disconnect_req(sender_badge, old_tag, received_cap);
}

static void handle_ipc_bench_req(void)
{
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_BENCH_IPC_ACK);
    bool do_cap_transfer = seL4_GetMR(PDMSGREG_BENCH_IPC_REQ_CAP_TRANSFER);

    int num_caps = 0;
    if (do_cap_transfer)
    {
        seL4_CPtr dummy_reply_cap;
        int error = vka_cspace_alloc(get_pd_component()->server_vka, &dummy_reply_cap);
        assert(error == 0);
        seL4_SetCap(0, dummy_reply_cap);
        num_caps = 1;
    }

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, num_caps, PDMSGREG_BENCH_IPC_ACK_END);
    return reply(tag);
}

/**
 * @brief clones a given PD into another PD, based on the resource configurations
 * this function highly couples all of the various GPI components, can we do any better?
 */
static void handle_clone_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    OSDB_PRINTF("Got clone request from client badge: ");
    badge_print(sender_badge);

    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_CLONE_REQ);
    int error = 0;

    // TODO: this might need to be under an allocation request
    seL4_MessageInfo_t tag;
    pd_component_registry_entry_t *sender_pd_data = pd_component_registry_get_entry_by_badge(sender_badge);
    SERVER_GOTO_IF_COND_BG(sender_pd_data == NULL, sender_badge, "Couldn't find sender PD with badge: ");

    seL4_Word src_pd_badge = seL4_GetBadge(0);
    pd_component_registry_entry_t *src_pd_data = pd_component_registry_get_entry_by_badge(src_pd_badge);
    SERVER_GOTO_IF_COND_BG(src_pd_data == NULL, src_pd_badge, "Couldn't find src PD with badge: ");

    seL4_Word shared_msg_mo_badge = seL4_GetBadge(1);
    mo_component_registry_entry_t *shared_msg_mo_data = mo_component_registry_get_entry_by_badge(shared_msg_mo_badge);
    SERVER_GOTO_IF_COND_BG(shared_msg_mo_data == NULL, shared_msg_mo_badge, "Couldn't find MO holding shared message, MO badge: ");

    // sel4utils_map_page(get_pd_component()->server_vka, )
    /* we have to do this because there is no ADS obj for the RT */
    pd_resource_config_t *resource_cfgs = (pd_resource_config_t *)vspace_map_pages(get_pd_component()->server_vspace, &shared_msg_mo_data->mo.frame_caps_in_root_task[0], NULL, seL4_AllRights, 1, seL4_PageBits, 1);
    SERVER_GOTO_IF_COND(resource_cfgs == NULL, "Couldn't map in resource configs\n");

    pd_component_registry_entry_t *new_entry;
    seL4_CPtr ret_cap;
    error = pd_component_allocate_pd(get_client_id_from_badge(sender_badge), false, &new_entry, &ret_cap);
    SERVER_GOTO_IF_ERR(error, "Failed to allocate a new PD\n");

    // for (int i = 0; i < MAX_RESOURCE_CONFIGS; i++)
    // {
    //     switch (resource_cfgs[i].type)
    //     {
    //     case GPICAP_TYPE_ADS:
    //         // call ADS component
    //         break;

    //     default:
    //         OSDB_PRINTF("Unhandled resource conifg type: %d\n", resource_cfgs[i].type);
    //         break;
    //     }
    // }

    vspace_unmap_pages(get_pd_component()->server_vspace, (void *)resource_cfgs, 1, seL4_PageBits, get_pd_component()->server_vka);

err_goto:
    tag = seL4_MessageInfo_new(error, 0, 0, PDMSGREG_CLONE_ACK_END);
    seL4_SetMR(PDMSGREG_FUNC, PD_FUNC_CLONE_REQ);

    if (resource_cfgs)
    {
        vspace_unmap_pages(get_pd_component()->server_vspace, (void *)resource_cfgs, 1, seL4_PageBits, get_pd_component()->server_vka);
    }

    return reply(tag);
}

/**
 * @brief The starting point for the pd server's thread.
 *
 */
void pd_component_handle(seL4_MessageInfo_t tag,
                         seL4_Word sender_badge,
                         cspacepath_t *received_cap,
                         seL4_MessageInfo_t *reply_tag) /* reply_tag not used right now*/
{
    enum pd_component_funcs func = seL4_GetMR(PDMSGREG_FUNC);

    switch (func)
    {
    case PD_FUNC_DISCONNECT_REQ:
        handle_disconnect_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_NEXT_SLOT_REQ:
        handle_next_slot_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_FREE_SLOT_REQ:
        handle_free_slot_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_ALLOC_EP_REQ:
        handle_alloc_ep_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_BADGE_EP_REQ:
        handle_badge_ep_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_SENDCAP_REQ:
        handle_send_cap_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_DUMP_REQ:
        handle_dump_cap_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_ADD_RDE_REQ:
        handle_add_rde_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_SHARE_RDE_REQ:
        handle_share_rde_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_REGISTER_SERV_REQ:
        handle_register_resource_manager_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_REGISTER_NS_REQ:
        handle_register_namespace_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_CREATE_RES_REQ:
        handle_create_resource_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_GIVE_RES_REQ:
        handle_give_resource_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_EXIT_REQ:
        handle_exit_req(sender_badge, tag, received_cap->capPtr);
        break;
    case PD_FUNC_BENCH_IPC_REQ:
        handle_ipc_bench_req();
        break;
    case PD_FUNC_CLONE_REQ:
        handle_clone_req(sender_badge, tag);
        break;
    default:
        gpi_panic(PDSERVS "Unknown func type.", (seL4_Word)func);
        break;
    }
}