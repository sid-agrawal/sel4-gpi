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

    cpu_component_registry_entry_t *current_ctx = get_cpu_component()->client_registry;

    while (current_ctx != NULL) {
        if ((seL4_Word)current_ctx == badge) {
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
    seL4_Word badge_val = gpi_new_badge(GPICAP_TYPE_CPU,
                                        0x00,
                                        0x00,
                                        get_cpu_component()->registry_n_entries);
    client_reg_ptr->cpu.cpu_obj_id = get_cpu_component()->registry_n_entries;
    get_ads_component()->registry_n_entries++;

    int error = vka_cnode_mint(&dest, &src, seL4_AllRights, badge_val);
    if (error)
    {
        printf(CPUSERVS "main: Failed to mint client badge %x.\n", badge_val);
        return;
    }
    /* Return this badged end point in the return message. */
    seL4_SetCap(0, dest.capPtr);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, 1);
    return reply(tag);
}

static void handle_start_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr frame_cap)
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

    error = cpu_start(&client_data->cpu, 0x00);
    if (error) {
        printf(CPUSERVS "main: Failed to start CPU.\n");
        return;
    }


    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_START_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, CPUMSGREG_START_ACK_END);
    return reply(tag);
}

#if 0

static void handle_config_req(seL4_Word sender_badge,
                              seL4_MessageInfo_t old_tag,
                              cspacepath_t ads_cap_path,
                              cspacepath_t cspace_root_cap_path)
{
    // Find the client - like start
    printf(CPUSERVS "-----main: Got config  request from client badge %x with %d extra caps.\n",
           sender_badge, seL4_MessageInfo_get_extraCaps(old_tag));
    // assert(seL4_MessageInfo_get_extraCaps(old_tag) == 1);
    
    assert(seL4_MessageInfo_get_label(old_tag) == 0);
    int error = 0;
    
    printf(CPUSERVS "capsUnwrapped: %d\n", seL4_MessageInfo_get_capsUnwrapped(old_tag));
    /* Find the client */
    cpu_component_registry_entry_t *client_data = cpu_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        printf(CPUSERVS "main: Failed to find client badge %x.\n",
               sender_badge);
        return;
    }
    printf(CPUSERVS "main: found client_data %x.\n", client_data);
    debug_cap_identify(CPUSERVS, ads_cap_path.capPtr);
    debug_cap_identify(CPUSERVS, ads_cap_path.capPtr+1);
    debug_cap_identify(CPUSERVS, cspace_root_cap_path.capPtr);

    printf(CPUSERVS "main: end of handle config request.\n");


    // /* Get the vspace for the ads */
    // ads_client_context_t ads_client_ctx;
    // ads_client_ctx.badged_server_ep_cspath = ads_cap_path;
    // seL4_Word ads_id;
    // error = ads_client_getID(&ads_client_ctx, &ads_id);
    // if (error) {
    //     printf(CPUSERVS "main: Failed to get ads ID.\n");
    //     return;
    // }
    // ads_component_registry_entry_t *asre = ads_component_registry_get_entry_by_badge(ads_id);
    // if (asre == NULL) {
    //     printf(CPUSERVS "main: Failed to find ads badge %x.\n",
    //            ads_id);
    //     return;
    // }

    // /* Get the vspace for the ads */
    // vspace_t *ads_vspace = asre->ads.vspace;





    
    // seL4_CNode cspace_root;
    // error = cpu_config_vspace(&client_data->cpu,
    //                           get_cpu_component()->server_vka,
    //                           ads_vspace,
    //                           cspace_root_cap_path.capPtr);
    // if (error) {
    //     printf(CPUSERVS "main: Failed to config from client badge %x.\n",
    //            sender_badge);
    //     return;
    // }
    printf(CPUSERVS "main: config done.\n");

    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CONFIG_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, CPUMSGREG_CONFIG_ACK_END);
    return reply(tag);
}
#endif

/**
 * @brief The starting point for the cpu server's thread.
 *
 */
void cpu_component_handle(seL4_MessageInfo_t tag,
                          seL4_Word sender_badge, 
                          cspacepath_t *received_cap,
                          seL4_MessageInfo_t *reply_tag)
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

    // case CPU_FUNC_CONFIG_REQ:
    //     /* TODO: Fix the args */
    //     handle_config_req(sender_badge, tag, received_cap, received_cap);
    //     break;

    default:
        ZF_LOGW(CPUSERVS "main: Unknown function %d requested.", func);
        break;
    }
}