/**
 * @file cpu_server.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the cpu server API from cpu_server.h.
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
#include <sel4gpi/cpu_parentapi.h>
#include <sel4gpi/cpu_server.h>


static cpu_server_context_t cpu_server;

cpu_server_context_t *get_cpu_server(void)
{
    return &cpu_server;
}

static inline seL4_MessageInfo_t recv(seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(get_cpu_server()->server_ep_obj.cptr,
                    sender_badge_ptr,
                    get_cpu_server()->server_thread.reply.cptr);
}

static inline void reply(seL4_MessageInfo_t tag)
{
    api_reply(get_cpu_server()->server_thread.reply.cptr, tag);
}


/**
 * @brief Insert a new client into the client registry Linked List.
 * 
 * @param new_node 
 */
static void cpu_server_registry_insert(cpu_server_registry_entry_t *new_node) {
        // TODO:Use a mutex


    cpu_server_registry_entry_t *head = get_cpu_server()->client_registry;

    if (head == NULL) {
        get_cpu_server()->client_registry = new_node;
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
 * @return cpu_server_registry_entry_t* 
 */
static cpu_server_registry_entry_t *ads_server_registry_get_entry_by_badge(seL4_Word badge){

    ads_server_registry_entry_t *current_ctx = get_ads_server()->client_registry;

    while (current_ctx != NULL) {
        if ((seL4_Word)current_ctx == badge) {
            break;
        }
        current_ctx = current_ctx->next;
    }
    return current_ctx;
}

static void handle_connect_req()
{
    printf(CPUSERVS "main: Got connect request from");

    /* Allocate a new registry entry for the client. */
    seL4_Word client_reg_ptr = (seL4_Word)malloc(sizeof(ads_server_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        printf(CPUSERVS "main: Failed to allocate new badge for client.\n");
        return;
   }
    memset((void *)client_reg_ptr, 0, sizeof(ads_server_registry_entry_t));
    ads_server_registry_insert((ads_server_registry_entry_t *)client_reg_ptr);

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src, dest;
    vka_cspace_make_path(get_ads_server()->server_vka,
                         get_ads_server()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_ads_server()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_ads_server()->server_vka, dest_cptr, &dest);

    int error = vka_cnode_mint(&dest, &src, seL4_AllRights, client_reg_ptr);
    if (error)
    {
        printf(CPUSERVS "main: Failed to mint client badge %x.\n",
               client_reg_ptr);
        return;
    }
    /* Return this badged end point in the return message. */
    seL4_SetCap(0, dest.capPtr);
    seL4_SetMR(CPUMSGREG_FUNC, FUNC_CONNECT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, CPUMSGREG_CONNECT_ACK_END);
    return reply(tag);
}

static void handle_start_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr frame_cap)
{
    printf(CPUSERVS "main: Got start request from client badge %x.\n",
           sender_badge);

    assert(seL4_MessageInfo_get_extraCaps(old_tag) == 1);
    int error;
    /* Find the client */
    ads_server_registry_entry_t *client_data = ads_server_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        printf(CPUSERVS "main: Failed to find client badge %x.\n",
               sender_badge);
        return;
    }
    printf(CPUSERVS "main: found client_data %x.\n", client_data);

    void *vaddr = (void *) seL4_GetMR(CPUMSGREG_START_REQ_VA);
    size_t size = (size_t) seL4_GetMR(CPUMSGREG_START_REQ_SZ);
    printf(CPUSERVS"main: vaddr %x, size %x\n", vaddr, size);

    error = ads_start(&client_data->ads, get_ads_server()->server_vka, vaddr, size, frame_cap, client_data->ads.vspace);
    if (error) {
        printf(CPUSERVS "main: Failed to start at vaddr:%lx sz: %lx to client badge %x.\n",
                vaddr, size, sender_badge);
        return;
    }


    sel4utils_walk_vspace(client_data->ads.vspace, NULL);
    seL4_SetMR(CPUMSGREG_FUNC, FUNC_START_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, CPUMSGREG_START_ACK_END);
    return reply(tag);
}

static void handle_config_req(seL4_Word sender_badge)
{
    // Find the client - like start
    printf(CPUSERVS "main: Got config  request from client badge %x.\n",
           sender_badge);

    /* Find the client */
    cpu_server_registry_entry_t *client_data = cpu_server_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        printf(CPUSERVS "main: Failed to find client badge %x.\n",
               sender_badge);
        return;
    }
    printf(CPUSERVS "main: found client_data %x.\n", client_data);



    // Make a new endpoint for the client to send messages to. like connect.
    printf(CPUSERVS "main: Making a new cap for the config.\n");
    /* Allocate a new registry entry for the client. */
    seL4_Word client_reg_ptr = (seL4_Word)malloc(sizeof(cpu_server_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        printf(CPUSERVS "main: Failed to allocate new badge for client.\n");
        return;
   }
    memset((void *)client_reg_ptr, 0, sizeof(cpu_server_registry_entry_t));
    cpu_server_registry_insert((cpu_server_registry_entry_t *)client_reg_ptr);


    // Do the actual config
    void *omit_vaddr = (void *) seL4_GetMR(CPUMSGREG_CONFIG_REQ_OMIT_VA) remove
    cpu_t src_cpu = client_data->cpu;
    cpu_t dst_cpu = ((cpu_server_registry_entry_t *)client_reg_ptr)->cpu;
    int error = cpu_config(get_cpu_server()->server_vspace,
                          &src_cpu,
                          get_cpu_server()->server_vka,
                          omit_vaddr,
                          &dst_cpu);
    if (error) {
        printf(CPUSERVS "main: Failed to config from client badge %x.\n",
               sender_badge);
        return;
    }
    printf(CPUSERVS "main: config done.\n");

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src_path, dest_path;
    vka_cspace_make_path(get_cpu_server()->server_vka,
                         get_cpu_server()->server_ep_obj.cptr, &src_path);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_cpu_server()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_cpu_server()->server_vka, dest_cptr, &dest_path);

    error = vka_cnode_mint(&dest_path, &src_path, seL4_AllRights, client_reg_ptr);
    if (error)
    {
        printf(CPUSERVS "main: Failed to mint client badge %x.\n",
               client_reg_ptr);
        return;
    }
    /* Return this badged end point in the return message. */
    seL4_SetCap(0, dest_path.capPtr);
    seL4_SetMR(CPUMSGREG_FUNC, FUNC_CONFIG_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, CPUMSGREG_CONFIG_ACK_END);
    return reply(tag);

}

/**
 * @brief The starting point for the ads server's thread.
 *
 */
void ads_server_main()
{
    seL4_MessageInfo_t tag;
    enum ads_server_funcs func;
    seL4_Error error = 0;
    size_t buff_len, bytes_written;

    /* The Parent will seL4_Call() to us, the Server, right after spawning us.
     * It will expect us to seL4_Reply() with an error status code - we will
     * send this Reply.
     *
     * First call seL4_Recv() to get the Reply cap back to the Parent, and then
     * seL4_Reply to report our status.
     */
    seL4_Word sender_badge;
    recv(&sender_badge);
    assert(sender_badge == CPU_SERVER_BADGE_PARENT_VALUE);

    seL4_SetMR(CPUMSGREG_FUNC, FUNC_SERVER_SPAWN_SYNC_ACK);
    tag = seL4_MessageInfo_new(error, 0, 0, CPUMSGREG_SPAWN_SYNC_ACK_END);
    reply(tag);

    /* If the bind failed, this thread has essentially failed its mandate, so
     * there is no reason to leave it scheduled. Kill it (to whatever extent
     * that is possible).
     */

    printf(CPUSERVS"ads_server_main: Got a call from the parent.\n");
    if (error != 0)
    {
        seL4_TCB_Suspend(get_ads_server()->server_thread.tcb.cptr);
    }


    printf(CPUSERVS"main: Entering main loop and accepting requests.\n");
    while (1) {
        /* Pre */
        seL4_CPtr received_cap;
        cspacepath_t received_cap_path;
            /* Get the frame cap from the message */
            vka_cspace_alloc(get_ads_server()->server_vka, &received_cap);
            vka_cspace_make_path(get_ads_server()->server_vka, received_cap, &received_cap_path);
            seL4_SetCapReceivePath(
                /* _service */ received_cap_path.root,
                /* index */ received_cap_path.capPtr,
                /* depth */ received_cap_path.capDepth);
        tag = recv(&sender_badge);
        printf(CPUSERVS "main: Got message from %x\n", sender_badge);

        func = seL4_GetMR(CPUMSGREG_FUNC);

        // if the badge is not set, then it has to be a new connection request.
        if (sender_badge == CPU_SERVER_BADGE_VALUE_EMPTY && func != FUNC_CONNECT_REQ){
            printf(CPUSERVS "main: Badge not set, but not a connect request.\n");
            continue;
        }

        /* Post */
        switch (func) {
        case FUNC_CONNECT_REQ:
            handle_connect_req();
            break;

        case FUNC_START_REQ:
            handle_start_req(sender_badge, tag, received_cap);
            break;

        case FUNC_CONFIG_REQ:
            handle_config_req(sender_badge);
            break;

        default:
            ZF_LOGW(CPUSERVS "main: Unknown function %d requested.", func);
            break;
        }
    }

    //serial_server_func_kill();
    /* After we break out of the loop, seL4_TCB_Suspend ourselves */
    ZF_LOGI(CPUSERVS"main: Suspending.");
    seL4_TCB_Suspend(get_ads_server()->server_thread.tcb.cptr);
}