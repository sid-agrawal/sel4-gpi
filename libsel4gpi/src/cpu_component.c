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
#include <sel4gpi/debug.h>

uint64_t cpu_assign_new_badge_and_objectID(cpu_component_registry_entry_t *reg)
{
    get_cpu_component()->registry_n_entries++;
    // Add the latest ID to the obj and to the badlge.
    seL4_Word badge_val = gpi_new_badge(GPICAP_TYPE_CPU,
                                        0x00,
                                        0x00,
                                        NSID_DEFAULT,
                                        get_cpu_component()->registry_n_entries);

    assert(badge_val != 0);
    reg->cpu.cpu_obj_id = get_cpu_component()->registry_n_entries;
    OSDB_PRINTF(CPU_DEBUG, "cpu_assign_new_badge_and_objectID: new badge: %lx\n", badge_val);
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
static void cpu_component_registry_insert(cpu_component_registry_entry_t *new_node)
{
    // TODO:Use a mutex

    cpu_component_registry_entry_t *head = get_cpu_component()->client_registry;

    if (head == NULL)
    {
        get_cpu_component()->client_registry = new_node;
        new_node->next = NULL;
        return;
    }

    while (head->next != NULL)
    {
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
cpu_component_registry_entry_t *cpu_component_registry_get_entry_by_badge(seL4_Word badge)
{

    uint64_t objectID = get_object_id_from_badge(badge);
    cpu_component_registry_entry_t *current_ctx = get_cpu_component()->client_registry;

    while (current_ctx != NULL)
    {
        if ((seL4_Word)current_ctx->cpu.cpu_obj_id == objectID)
        {
            break;
        }
        current_ctx = current_ctx->next;
    }
    return current_ctx;
}

// (XXX): Somwehere here we should call cpu_new
void cpu_handle_allocation_request(seL4_Word sender_badge, seL4_MessageInfo_t *reply_tag)
{
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Got connect request\n");

    /* Allocate a new registry entry for the client. */
    cpu_component_registry_entry_t *client_reg_ptr = malloc(sizeof(cpu_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to allocate new badge for client.\n");
        return;
    }
    memset((void *)client_reg_ptr, 0, sizeof(cpu_component_registry_entry_t));
    cpu_component_registry_insert(client_reg_ptr);

    /* Createa a new CPU object */

    int error = cpu_new(&client_reg_ptr->cpu,
                        get_cpu_component()->server_vka);
    if (error)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to create new CPU object\n");
        return;
    }

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
    uint32_t client_id = get_client_id_from_badge(sender_badge);

    // (XXX) Linh: this is not very nice as we're coupling the PD and CPU components
    osmosis_pd_cap_t *res = pd_add_resource_by_id(client_id, GPICAP_TYPE_CPU, get_object_id_from_badge(badge));
    if (res)
    {
        res->slot_in_RT_Debug = dest_cptr;
        badge = set_client_id_to_badge(badge, client_id);
    }
    error = vka_cnode_mint(&dest,
                           &src,
                           seL4_AllRights,
                           badge);
    if (error)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to mint client badge %lx.\n", badge);
        return;
    }
    /* Return this badged end point in the return message. */
    seL4_SetCap(0, dest.capPtr);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, 1);
    return reply(tag);
}

static void handle_start_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr received_cap)
{
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Got start request from client badge %lx.\n",
                sender_badge);

    int error;
    /* Find the client */
    cpu_component_registry_entry_t *client_data = cpu_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to find client badge %lx.\n", sender_badge);
        return;
    }
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: found client_data %p.\n", client_data);

    seL4_Word init_stack = seL4_GetMR(CPUMSGREG_START_INIT_STACK_ADDR);
    seL4_Word arg0 = seL4_GetMR(CPUMSGREG_START_ARG0);

    error = cpu_start(&client_data->cpu,
                      (void *)seL4_GetMR(CPUMSGREG_START_FUNC_VADDR),
                      (void *)init_stack,
                      arg0);
    if (error)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to start CPU.\n");
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
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "-----main: Got config  request from:");
    badge_print(sender_badge);

    assert(seL4_MessageInfo_ptr_get_capsUnwrapped(&old_tag) >= 2);
    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    int error = 0;

    /* Find the client */
    cpu_component_registry_entry_t *client_data = cpu_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to find client badge %lx.\n",
                    sender_badge);
        return;
    }

    pd_component_registry_entry_t *pd_data = pd_component_registry_get_entry_by_badge(seL4_GetBadge(0));
    if (pd_data == NULL)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to PD data for:\n");
        badge_print(seL4_GetBadge(0));
        return;
    }

    /* Get Fault EP */
    seL4_CPtr fault_ep = seL4_GetMR(CPUMSGREG_CONFIG_FAULT_EP);

    /* Get IPC buf addr */
    seL4_Word ipc_buf_addr = seL4_GetMR(CPUMSGREG_CONFIG_IPC_BUF_ADDR);

    /* get cnode guard*/
    seL4_Word cnode_guard = seL4_GetMR(CPUMSGREG_CONFIG_CNODE_GUARD);

    /* Get the vspace for the ads */
    seL4_Word ads_cap_badge = seL4_GetBadge(1);
    ads_component_registry_entry_t *asre = ads_component_registry_get_entry_by_badge(ads_cap_badge);
    if (asre == NULL)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to find ADS data for:\n");
        badge_print(ads_cap_badge);
        return;
    }

    // OSDB_PRINTF(CPU_DEBUG, CPUSERVS "Found ads_data with object ID: %u.\n", asre->ads.ads_obj_id);
    // /* Get the vspace for the ads */
    vspace_t *ads_vspace = asre->ads.vspace;

    seL4_Word ipc_buf_mo_badge = seL4_GetBadge(2);
    // it's ok if this doesn't exist
    mo_component_registry_entry_t *ipc_mo_data = mo_component_registry_get_entry_by_badge(ipc_buf_mo_badge);
    seL4_CPtr ipc_buf_frame = ipc_mo_data == NULL ? seL4_CapNull : ipc_mo_data->mo.frame_caps_in_root_task[0].cap;

    seL4_CNode cspace_root = pd_data->pd.proc.cspace.cptr;

    error = cpu_config_vspace(&client_data->cpu,
                              get_cpu_component()->server_vka,
                              ads_vspace,
                              cspace_root,
                              cnode_guard,
                              fault_ep,
                              ipc_buf_frame,
                              ipc_buf_addr);
    if (error)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to config from client badge:");
        badge_print(sender_badge);
        assert(0);
        return;
    }
    client_data->cpu.binded_ads_id = asre->ads.ads_obj_id;
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: config done.\n");

    pd_configure(&pd_data->pd, "", &asre->ads, &client_data->cpu);

    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CONFIG_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, CPUMSGREG_CONFIG_ACK_END);
    return reply(tag);
}

static void handle_change_vspace_req(seL4_Word sender_badge,
                                     seL4_MessageInfo_t old_tag,
                                     seL4_CPtr received_cap)
{
    // Find the client - like start
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "-----main: Got change vsspace  request from:");
    badge_print(sender_badge);

    OSDB_PRINTF(CPU_DEBUG, CPUSERVS " received_cap: ");
    // debug_cap_identify("", received_cap);

    assert(seL4_MessageInfo_get_extraCaps(old_tag) == 1);
    assert(seL4_MessageInfo_ptr_get_capsUnwrapped(&old_tag) == 1);
    assert(seL4_MessageInfo_get_label(old_tag) == 0);

    int error = 0;

    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "capsUnwrapped: %lu\n", seL4_MessageInfo_get_capsUnwrapped(old_tag));
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "extraCap: %lu\n", seL4_MessageInfo_ptr_get_extraCaps(&old_tag));
    // for (int i = 0; i < 5; i++)
    // {
    //     OSDB_PRINTF(CPU_DEBUG, CPUSERVS "MR[%d] = %lx\n", i, seL4_GetBadge(i));
    // }

    /* Find the client */
    cpu_component_registry_entry_t *client_data = cpu_component_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to find client badge %lx.\n",
                    sender_badge);
        assert(0);
        return;
    }

    /* Get the vspace for the ads */
    seL4_Word ads_cap_badge = seL4_GetBadge(0);
    ads_component_registry_entry_t *ads_data = ads_component_registry_get_entry_by_badge(ads_cap_badge);
    if (ads_data == NULL)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to find ads badge %lx.\n", ads_cap_badge);
        assert(0);
        return;
    }

    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "Found ads_data with object ID: %u.\n", ads_data->ads.ads_obj_id);
    // /* Get the vspace for the ads */
    vspace_t *ads_vspace = ads_data->ads.vspace;

    error = cpu_change_vspace(&client_data->cpu,
                              get_cpu_component()->server_vka,
                              ads_vspace);
    if (error)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to config from client badge:");
        badge_print(sender_badge);
        assert(0);
        return;
    }
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: config done.\n");

    pd_component_registry_entry_t *pd_data = pd_component_registry_get_entry_by_id(get_client_id_from_badge(sender_badge));
    pd_data->pd.init_data->binded_ads_ns_id = ads_data->ads.ads_obj_id;
    client_data->cpu.binded_ads_id = ads_data->ads.ads_obj_id;

    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CONFIG_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 0, CPUMSGREG_CHANGE_VSPACE_ACK_END);

    return reply(tag);
}

/**
 * @brief Lookup the client registry entry for the given objectID
 *
 * @param res_id
 * @return cpu_component_registry_entry_t*
 */
cpu_component_registry_entry_t *cpu_component_registry_get_entry_by_id(seL4_Word objectID)
{
    cpu_component_registry_entry_t *current_ctx = get_cpu_component()->client_registry;

    while (current_ctx != NULL)
    {
        if (current_ctx->cpu.cpu_obj_id == objectID)
        {
            break;
        }
        current_ctx = current_ctx->next;
    }
    return current_ctx;
}

int forge_cpu_cap_from_tcb(sel4utils_process_t *process, // Change this to the sel4utils_thread_t
                           vka_t *vka, uint32_t client_id,
                           seL4_CPtr *cap_ret, uint32_t *cpu_obj_id_ret)
{

    assert(process != NULL);
    /* Allocate a new registry entry for the client. */
    cpu_component_registry_entry_t *client_reg_ptr = malloc(sizeof(cpu_component_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to allocate new badge for client.\n");
        return 1;
    }
    memset((void *)client_reg_ptr, 0, sizeof(cpu_component_registry_entry_t));

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src, dest;
    vka_cspace_make_path(get_cpu_component()->server_vka,
                         get_cpu_component()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_cpu_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_cpu_component()->server_vka, dest_cptr, &dest);

    /* Update the info in the registry entry. */
    seL4_Word badge = cpu_assign_new_badge_and_objectID(client_reg_ptr);
    badge = set_client_id_to_badge(badge, client_id);
    cpu_component_registry_insert(client_reg_ptr);

    // (XXX) A lot more will go here.
    client_reg_ptr->cpu.thread = process->thread;
    // client_reg_ptr->cpu.tls_base = &process->thread.tls_base;
    client_reg_ptr->cpu.cspace = process->cspace.cptr;

    int error = vka_cnode_mint(&dest,
                               &src,
                               seL4_AllRights,
                               badge);
    if (error)
    {
        OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Failed to mint client badge %lx.\n", badge);
        return 1;
    }
    OSDB_PRINTF(CPU_DEBUG, CPUSERVS "main: Forged a new CPU cap(EP: %lx) with badge value: %lx IPC_Buff %p stack %p\n",
                dest.capPtr, badge, client_reg_ptr->cpu.thread.ipc_buffer_addr, client_reg_ptr->cpu.thread.stack_top);

    *cap_ret = dest_cptr;
    if (cpu_obj_id_ret)
    {
        *cpu_obj_id_ret = get_object_id_from_badge(badge);
    }
    return 0;
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
    case CPU_FUNC_CHANGE_VSPACE_REQ:
        handle_change_vspace_req(sender_badge, tag, received_cap->capPtr);
        break;
    default:
        gpi_panic(CPUSERVS "Unknown func type.", (seL4_Word)func);
        break;
    }
}