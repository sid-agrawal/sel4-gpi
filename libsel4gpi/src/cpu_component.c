/**
 * @file cpu_component.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the cpu server API from cpu_component.h.
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

#include <sel4gpi/cpu_clientapi.h>
#include <sel4gpi/cpu_component.h>

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/gpi_server.h>
#include <sel4gpi/badge_usage.h>

uint64_t cpu_assign_new_badge_and_objectID(cpu_component_registry_entry_t *reg) {
    get_cpu_component()->registry_n_entries++;
    // Add the latest ID to the obj and to the badlge.
    seL4_Word badge_val = gpi_new_badge(GPICAP_TYPE_CPU,
                                        0x00,
                                        0x00,
                                        get_cpu_component()->registry_n_entries);

    assert(badge_val != 0);
    reg->cpu.cpu_obj_id = get_cpu_component()->registry_n_entries;
    printf("cpu_assign_new_badge_and_objectID: new badge: %lx\n", badge_val);
    return badge_val;
}
cpu_component_context_t *get_cpu_component(void)
{
    return &get_gpi_server()->cpu_component;
}

static inline seL4_MessageInfo_t recv(seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(get_cpu_component()->server_ep_obj.cptr,
                    sender_badge_ptr,
                    get_cpu_component()->server_thread.reply.cptr);
}

static inline void reply(seL4_MessageInfo_t tag)
{
    api_reply(get_cpu_component()->server_thread.reply.cptr, tag);
}


/**
 * @brief Insert a new client into the client registry Linked List.
 * 
 * @param new_node 
 */
static void cpu_component_registry_insert(cpu_component_registry_entry_t *new_node) {
        // TODO:Use a mutex


    cpu_component_registry_entry_t *head = get_cpu_component()->client_registry;

    if (head == NULL) {
        get_cpu_component()->client_registry = new_node;
        new_node->next = NULL;
        return;
    }

    while (head->next != NULL) {
        head = head->next;
    }
    head->next = new_node;
    new_node->next = NULL;
}

/**
 * @brief Lookup the client registry entry for the give badge.
 * 
 * @param badge 
 * @return cpu_component_registry_entry_t* 
 */
static cpu_component_registry_entry_t *cpu_component_registry_get_entry_by_badge(seL4_Word badge){

    uint64_t objectID = get_object_id_from_badge(badge);
    cpu_component_registry_entry_t *current_ctx = get_cpu_component()->client_registry;

    while (current_ctx != NULL) {
        if ((seL4_Word)current_ctx->cpu.cpu_obj_id == objectID) {
            break;
        }
        current_ctx = current_ctx->next;
    }
    return current_ctx;
}

void cpu_handle_allocation_request(seL4_MessageInfo_t *reply_tag)
{
    printf(CPUSERVS "main: Got connect request\n");

    /* Allocate a new registry entry for the client. */
    cpu_component_registry_entry_t *client_reg_ptr = malloc(sizeof(cpu_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        printf(CPUSERVS "main: Failed to allocate new badge for client.\n");
        return;
    }
    memset((void *)client_reg_ptr, 0, sizeof(cpu_component_registry_entry_t));
    cpu_component_registry_insert(client_reg_ptr);

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src, dest;
    vka_cspace_make_path(get_cpu_component()->server_vka,
                         get_cpu_component()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_cpu_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_cpu_component()->server_vka, dest_cptr, &dest);

    // Add the latest ID to the obj and to the badlge.
    seL4_Word badge = cpu_assign_new_badge_and_objectID(client_reg_ptr);
    int error = vka_cnode_mint(&dest,
                               &src,
                               seL4_AllRights,
                               badge);
    if (error)
    {
        printf(CPUSERVS "main: Failed to mint client badge %lx.\n", badge);
        return;
    }
    /* Return this badged end point in the return message. */
    seL4_SetCap(0, dest.capPtr);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, 1);
    return reply(tag);
}

static void handle_start_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    printf(CPUSERVS "main: Got start request from client badge %x.\n",
           sender_badge);

    int error;
    /* Find the client */
    cpu_component_registry_entry_t *client_data = cpu_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        printf(CPUSERVS "main: Failed to find client badge %x.\n",
               sender_badge);
        return;
    }
    printf(CPUSERVS "main: found client_data %x.\n", client_data);
    for (int i = 0; i < 5; i++)
    {
        printf(CPUSERVS "MR[%d] = %lx\n", i, seL4_GetMR(i));
    }

    error = cpu_start(&client_data->cpu, (sel4utils_thread_entry_fn)seL4_GetMR(1));
    if (error) {
        printf(CPUSERVS "main: Failed to start CPU.\n");
        return;
    }


    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_START_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, CPUMSGREG_START_ACK_END);
    return reply(tag);
}


static void handle_config_req(seL4_Word sender_badge,
                              seL4_MessageInfo_t old_tag,
                              seL4_CPtr received_cap)
{
    // Find the client - like start
    printf(CPUSERVS "-----main: Got config  request from:");
    badge_print(sender_badge);

    printf(CPUSERVS " received_cap: ");
    debug_cap_identify("", received_cap);

    assert(seL4_MessageInfo_get_extraCaps(old_tag) == 2);
    assert(seL4_MessageInfo_ptr_get_capsUnwrapped(&old_tag) == 1);
    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    int error = 0;

    printf(CPUSERVS "capsUnwrapped: %d\n", seL4_MessageInfo_get_capsUnwrapped(old_tag));
    printf(CPUSERVS "extraCap: %d\n", seL4_MessageInfo_ptr_get_extraCaps(&old_tag));
    for (int i = 0; i < 5; i++)
    {
        printf(CPUSERVS "MR[%d] = %lx\n", i, seL4_GetBadge(i));
    }

    /* Find the client */
    cpu_component_registry_entry_t *client_data = cpu_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        printf(CPUSERVS "main: Failed to find client badge %x.\n",
               sender_badge);
        assert(0);
        return;
    }

    /* Get the vspace for the ads */
    seL4_Word ads_cap_badge = seL4_GetBadge(0);
    ads_t ads;
    ads_component_registry_entry_t *asre = ads_component_registry_get_entry_by_badge(ads_cap_badge);
    if (asre == NULL)
    {
        printf(CPUSERVS "main: Failed to find ads badge %x.\n", ads_cap_badge);
        assert(0);
        return;
    }

    printf(CPUSERVS "Found ads_data with object ID: %x.\n", asre->ads.ads_obj_id);
    // /* Get the vspace for the ads */
    vspace_t *ads_vspace = asre->ads.vspace;

    seL4_CNode cspace_root = received_cap;
    error = cpu_config_vspace(&client_data->cpu,
                              get_cpu_component()->server_vka,
                              ads_vspace,
                              cspace_root);
    if (error)
    {
        printf(CPUSERVS "main: Failed to config from client badge:");
        badge_print(sender_badge);
        assert(0);
        return;
    }
    printf(CPUSERVS "main: config done.\n");

    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CONFIG_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, CPUMSGREG_CONFIG_ACK_END);
    return reply(tag);
}

/**
 * @brief The starting point for the cpu server's thread.
 *
 */
void cpu_component_handle(seL4_MessageInfo_t tag,
                          seL4_Word sender_badge, 
                          cspacepath_t *received_cap,
                          seL4_MessageInfo_t *reply_tag) /* reply_tag not used right now*/
{
    enum cpu_component_funcs func;
    seL4_Error error = 0;
    /* Post */
    func = seL4_GetMR(CPUMSGREG_FUNC);
    switch (func)
    {
    case CPU_FUNC_START_REQ:
        handle_start_req(sender_badge, tag, received_cap->capPtr);
        break;

    case CPU_FUNC_CONFIG_REQ:
        handle_config_req(sender_badge, tag, received_cap->capPtr);
        break;
    default:
        gpi_panic(ADSSERVS "Unknown cap type.");
        break;
    }
}