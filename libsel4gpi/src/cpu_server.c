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

#include <sel4gpi/ads_clientapi.h>

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
static cpu_server_registry_entry_t *cpu_server_registry_get_entry_by_badge(seL4_Word badge){

    cpu_server_registry_entry_t *current_ctx = get_cpu_server()->client_registry;

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
    printf(CPUSERVS "main: Got connect request\n");

    /* Allocate a new registry entry for the client. */
    seL4_Word client_reg_ptr = (seL4_Word)malloc(sizeof(cpu_server_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        printf(CPUSERVS "main: Failed to allocate new badge for client.\n");
        return;
   }
    memset((void *)client_reg_ptr, 0, sizeof(cpu_server_registry_entry_t));
    cpu_server_registry_insert((cpu_server_registry_entry_t *)client_reg_ptr);

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src, dest;
    vka_cspace_make_path(get_cpu_server()->server_vka,
                         get_cpu_server()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_cpu_server()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_cpu_server()->server_vka, dest_cptr, &dest);

    int error = vka_cnode_mint(&dest, &src, seL4_AllRights, client_reg_ptr);
    if (error)
    {
        printf(CPUSERVS "main: Failed to mint client badge %x.\n",
               client_reg_ptr);
        return;
    }
    /* Return this badged end point in the return message. */
    seL4_SetCap(0, dest.capPtr);
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CONNECT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, CPUMSGREG_CONNECT_ACK_END);
    return reply(tag);
}

static void handle_start_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr frame_cap)
{
    printf(CPUSERVS "main: Got start request from client badge %x.\n",
           sender_badge);

    int error;
    /* Find the client */
    cpu_server_registry_entry_t *client_data = cpu_server_registry_get_entry_by_badge(sender_badge);
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
    cpu_server_registry_entry_t *client_data = cpu_server_registry_get_entry_by_badge(sender_badge);
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
    // ads_server_registry_entry_t *asre = ads_server_registry_get_entry_by_badge(ads_id);
    // if (asre == NULL) {
    //     printf(CPUSERVS "main: Failed to find ads badge %x.\n",
    //            ads_id);
    //     return;
    // }

    // /* Get the vspace for the ads */
    // vspace_t *ads_vspace = asre->ads.vspace;





    
    // seL4_CNode cspace_root;
    // error = cpu_config_vspace(&client_data->cpu,
    //                           get_cpu_server()->server_vka,
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

/**
 * @brief The starting point for the cpu server's thread.
 *
 */
void cpu_server_main()
{
    seL4_MessageInfo_t tag;
    enum cpu_server_funcs func;
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

    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_SERVER_SPAWN_SYNC_ACK);
    tag = seL4_MessageInfo_new(error, 0, 0, CPUMSGREG_SPAWN_SYNC_ACK_END);
    reply(tag);

    /* If the bind failed, this thread has essentially failed its mandate, so
     * there is no reason to leave it scheduled. Kill it (to whatever extent
     * that is possible).
     */

    printf(CPUSERVS"cpu_server_main: Got a call from the parent.\n");
    if (error != 0)
    {
        seL4_TCB_Suspend(get_cpu_server()->server_thread.tcb.cptr);
    }


    printf(CPUSERVS"main: Entering main loop and accepting requests.\n");
    while (1) {
        /* Pre */
        int error = 0;
        cspacepath_t received_cap_path;
        /* Get the frame cap from the message */
        error = vka_cspace_alloc_path(get_cpu_server()->server_vka, &received_cap_path); // use "vka_cspace_alloc_path" instgead
        assert(error == 0);
        printf(CPUSERVS "==========main: allocated 1st slot: cptr: %d root: %d depth: %d\n",
               received_cap_path.capPtr, received_cap_path.capDepth, received_cap_path.root);
        debug_cap_identify(CPUSERVS, received_cap_path.capPtr);

        /* Allocate another csapce slot */
        cspacepath_t received_cap_path_2;
        error = vka_cspace_alloc_path(get_cpu_server()->server_vka, &received_cap_path_2); // use "vka_cspace_alloc_path" instgead
        assert(error == 0);
        printf(CPUSERVS "==========main: allocated 1st slot: cptr: %d root: %d depth: %d\n",
               received_cap_path_2.capPtr, received_cap_path_2.capDepth, received_cap_path_2.root);
        debug_cap_identify(CPUSERVS, received_cap_path_2.capPtr);
        

        seL4_CPtr min_cptr = MIN(received_cap_path.capPtr, received_cap_path_2.capPtr);
        
        seL4_SetCapReceivePath(
            /* _service */ received_cap_path.root,
            /* index */ min_cptr,
            /* depth */ received_cap_path.capDepth);

        tag = recv(&sender_badge);
        assert(seL4_MessageInfo_get_extraCaps(tag) == 2);
        printf(CPUSERVS "------------main: Got message from %x with extraCap %d label: %d\n",
               sender_badge,
               seL4_MessageInfo_get_extraCaps(tag),
               seL4_MessageInfo_get_label(tag));

        debug_cap_identify(CPUSERVS, received_cap_path.capPtr);
        debug_cap_identify(CPUSERVS, received_cap_path_2.capPtr);

        func = seL4_GetMR(CPUMSGREG_FUNC);

        // if the badge is not set, then it has to be a new connection request.
        if (sender_badge == CPU_SERVER_BADGE_VALUE_EMPTY && func != CPU_FUNC_CONNECT_REQ){
            printf(CPUSERVS "main: Badge not set, but not a connect request.\n");
            continue;
        }

        /* Post */
        switch (func) {
        case CPU_FUNC_CONNECT_REQ:
            handle_connect_req();
            break;

        case CPU_FUNC_START_REQ:
            handle_start_req(sender_badge, tag, received_cap_path.capPtr);
            break;

        case CPU_FUNC_CONFIG_REQ:
            /* TODO: Fix the args */
            handle_config_req(sender_badge, tag, received_cap_path, received_cap_path_2);
            break;

        default:
            ZF_LOGW(CPUSERVS "main: Unknown function %d requested.", func);
            break;
        }
    }

    //serial_server_func_kill();
    /* After we break out of the loop, seL4_TCB_Suspend ourselves */
    ZF_LOGI(CPUSERVS"main: Suspending.");
    seL4_TCB_Suspend(get_cpu_server()->server_thread.tcb.cptr);
}