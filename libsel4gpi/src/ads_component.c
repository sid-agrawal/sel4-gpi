/**
 * @file ads_server.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the ads server API from ads_server.h.
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

#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/ads_server.h>
#include <sel4gpi/gpi_server.h>


ads_component_context_t *get_ads_component(void)
{
    return &get_gpi_server()->ads_server;
}

static inline seL4_MessageInfo_t recv(seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(get_ads_component()->server_ep_obj.cptr,
                    sender_badge_ptr,
                    get_ads_component()->server_thread.reply.cptr);
}

static inline void reply(seL4_MessageInfo_t tag)
{
    api_reply(get_ads_component()->server_thread.reply.cptr, tag);
}


/**
 * @brief Insert a new client into the client registry Linked List.
 * 
 * @param new_node 
 */
static void ads_server_registry_insert(ads_server_registry_entry_t *new_node) {
        // TODO:Use a mutex


    ads_server_registry_entry_t *head = get_ads_component()->client_registry;

    if (head == NULL) {
        get_ads_component()->client_registry = new_node;
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
 * @return ads_server_registry_entry_t* 
 */
ads_server_registry_entry_t *ads_server_registry_get_entry_by_badge(seL4_Word badge){

    ads_server_registry_entry_t *current_ctx = get_ads_component()->client_registry;

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
    printf(ADSSERVS "main: Got connect request from");

    /* Allocate a new registry entry for the client. */
    seL4_Word client_reg_ptr = (seL4_Word)malloc(sizeof(ads_server_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        printf(ADSSERVS "main: Failed to allocate new badge for client.\n");
        return;
   }
    memset((void *)client_reg_ptr, 0, sizeof(ads_server_registry_entry_t));
    ads_server_registry_insert((ads_server_registry_entry_t *)client_reg_ptr);

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src, dest;
    vka_cspace_make_path(get_ads_component()->server_vka,
                         get_ads_component()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_ads_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_ads_component()->server_vka, dest_cptr, &dest);

    int error = vka_cnode_mint(&dest, &src, seL4_AllRights, client_reg_ptr);
    if (error)
    {
        printf(ADSSERVS "main: Failed to mint client badge %x.\n",
               client_reg_ptr);
        return;
    }
    /* Return this badged end point in the return message. */
    seL4_SetCap(0, dest.capPtr);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_CONNECT_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, ADSMSGREG_CONNECT_ACK_END);
    return reply(tag);
}

static void handle_getid_req(seL4_Word sender_badge)
{
    printf(ADSSERVS "main: Got getID request from client badge %x.\n",
           sender_badge);

    int error;
    /* Find the client */
    ads_server_registry_entry_t *client_data = ads_server_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        printf(ADSSERVS "main: Failed to find client badge %x.\n",
               sender_badge);
        return;
    }
    printf(ADSSERVS "main: found client_data %x.\n", client_data);

    assert(sender_badge == (seL4_Word)client_data);
    
    /* Get the ID */
    seL4_Word id = sender_badge;
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_GETID_ACK);
    seL4_SetMR(ADSMSGREG_GETID_ACK_ID, id);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_GETID_ACK_END);
    return reply(tag);
}

static void handle_attach_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag, seL4_CPtr frame_cap)
{
    printf(ADSSERVS "main: Got attach request from client badge %x.\n",
           sender_badge);

    assert(seL4_MessageInfo_get_extraCaps(old_tag) == 1);
    int error;
    /* Find the client */
    ads_server_registry_entry_t *client_data = ads_server_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        printf(ADSSERVS "main: Failed to find client badge %x.\n",
               sender_badge);
        return;
    }
    printf(ADSSERVS "main: found client_data %x.\n", client_data);

    void *vaddr = (void *) seL4_GetMR(ADSMSGREG_ATTACH_REQ_VA);
    size_t size = (size_t) seL4_GetMR(ADSMSGREG_ATTACH_REQ_SZ);
    printf(ADSSERVS"main: vaddr %x, size %x\n", vaddr, size);

    error = ads_attach(&client_data->ads, get_ads_component()->server_vka, vaddr, size, frame_cap, client_data->ads.vspace);
    if (error) {
        printf(ADSSERVS "main: Failed to attach at vaddr:%lx sz: %lx to client badge %x.\n",
                vaddr, size, sender_badge);
        return;
    }


    // sel4utils_walk_vspace(client_data->ads.vspace, NULL);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_ATTACH_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_ATTACH_ACK_END);
    return reply(tag);
}

static void handle_testing_req(seL4_Word sender_badge, seL4_MessageInfo_t old_tag)
{
    printf(ADSSERVS "main: Got testing request from client badge %x."
    " extraCaps: %d capsUnWrapped %d\n",
           sender_badge, seL4_MessageInfo_get_extraCaps(old_tag), 
           seL4_MessageInfo_get_capsUnwrapped(old_tag));
    
    for (int i = 0; i < 5; i++) {
        printf(ADSSERVS "MR[%d] = %x\n", i, seL4_GetBadge(i));
    }

    // sel4utils_walk_vspace(client_data->ads.vspace, NULL);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_TESTING_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_TESTING_ACK_END);
    return reply(tag);
}


static void handle_clone_req(seL4_Word sender_badge)
{
    // Find the client - like attach
    printf(ADSSERVS "main: Got clone  request from client badge %x.\n",
           sender_badge);

    /* Find the client */
    ads_server_registry_entry_t *client_data = ads_server_registry_get_entry_by_badge(sender_badge);
    if (client_data == NULL)
    {
        printf(ADSSERVS "main: Failed to find client badge %x.\n",
               sender_badge);
        return;
    }
    printf(ADSSERVS "main: found client_data %x.\n", client_data);



    // Make a new endpoint for the client to send messages to. like connect.
    printf(ADSSERVS "main: Making a new cap for the clone.\n");
    /* Allocate a new registry entry for the client. */
    seL4_Word client_reg_ptr = (seL4_Word)malloc(sizeof(ads_server_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        printf(ADSSERVS "main: Failed to allocate new badge for client.\n");
        return;
   }
    memset((void *)client_reg_ptr, 0, sizeof(ads_server_registry_entry_t));
    ads_server_registry_insert((ads_server_registry_entry_t *)client_reg_ptr);


    // Do the actual clone
    void *omit_vaddr = (void *) seL4_GetMR(ADSMSGREG_CLONE_REQ_OMIT_VA);
    ads_t src_ads = client_data->ads;
    ads_t dst_ads = ((ads_server_registry_entry_t *)client_reg_ptr)->ads;
    int error = ads_clone(get_ads_component()->server_vspace,
                          &src_ads,
                          get_ads_component()->server_vka,
                          omit_vaddr,
                          &dst_ads);
    if (error) {
        printf(ADSSERVS "main: Failed to clone from client badge %x.\n",
               sender_badge);
        return;
    }
    printf(ADSSERVS "main: Clone done. Badge: %x\n", client_reg_ptr);

    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src_path, dest_path;
    vka_cspace_make_path(get_ads_component()->server_vka,
                         get_ads_component()->server_ep_obj.cptr, &src_path);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_ads_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_ads_component()->server_vka, dest_cptr, &dest_path);

    error = vka_cnode_mint(&dest_path, &src_path, seL4_AllRights, client_reg_ptr);
    if (error)
    {
        printf(ADSSERVS "main: Failed to mint client badge %x.\n",
               client_reg_ptr);
        return;
    }
    /* Return this badged end point in the return message. */
    seL4_SetCap(0, dest_path.capPtr);
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_CLONE_ACK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(error, 0, 1, ADSMSGREG_CLONE_ACK_END);
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
    assert(sender_badge == ADS_SERVER_BADGE_PARENT_VALUE);

    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_SERVER_SPAWN_SYNC_ACK);
    tag = seL4_MessageInfo_new(error, 0, 0, ADSMSGREG_SPAWN_SYNC_ACK_END);
    reply(tag);

    /* If the bind failed, this thread has essentially failed its mandate, so
     * there is no reason to leave it scheduled. Kill it (to whatever extent
     * that is possible).
     */

    printf(ADSSERVS"ads_server_main: Got a call from the parent.\n");
    if (error != 0)
    {
        seL4_TCB_Suspend(get_ads_component()->server_thread.tcb.cptr);
    }


    printf(ADSSERVS"main: Entering main loop and accepting requests.\n");
    while (1) {
        /* Pre */
        seL4_CPtr received_cap;
        cspacepath_t received_cap_path;
            /* Get the frame cap from the message */
            vka_cspace_alloc(get_ads_component()->server_vka, &received_cap);
            vka_cspace_make_path(get_ads_component()->server_vka, received_cap, &received_cap_path);
            seL4_SetCapReceivePath(
                /* _service */ received_cap_path.root,
                /* index */ received_cap_path.capPtr,
                /* depth */ received_cap_path.capDepth);
        tag = recv(&sender_badge);
        printf(ADSSERVS "main: Got message from %x\n", sender_badge);

        func = seL4_GetMR(ADSMSGREG_FUNC);

        // if the badge is not set, then it has to be a new connection request.
        if (sender_badge == ADS_SERVER_BADGE_VALUE_EMPTY && func != ADS_FUNC_CONNECT_REQ){
            printf(ADSSERVS "main: Badge not set, but not a connect request.\n");
            continue;
        }

        /* Post */
        switch (func) {
        case ADS_FUNC_CONNECT_REQ:
            handle_connect_req();
            break;

        case ADS_FUNC_GETID_REQ:
            handle_getid_req(sender_badge);
            break;

        case ADS_FUNC_CLONE_REQ:
            handle_clone_req(sender_badge);
            break;

        case ADS_FUNC_ATTACH_REQ:
            handle_attach_req(sender_badge, tag, received_cap);
            break;

        case ADS_FUNC_TESTING_REQ:
            handle_testing_req(sender_badge, tag);
            break;

        default:
            ZF_LOGW(ADSSERVS "main: Unknown function %d requested.", func);
            break;
        }
    }

    //serial_server_func_kill();
    /* After we break out of the loop, seL4_TCB_Suspend ourselves */
    ZF_LOGI(ADSSERVS"main: Suspending.");
    seL4_TCB_Suspend(get_ads_component()->server_thread.tcb.cptr);
}

int forge_ads_cap_from_vspace(vspace_t *vspace, vka_t *vka, seL4_CPtr *cap_ret){

    /* Allocate a new registry entry for the client. */
    seL4_Word client_reg_ptr = (seL4_Word)malloc(sizeof(ads_server_registry_entry_t));
    if (client_reg_ptr == 0)
    {
        printf(ADSSERVS "main: Failed to allocate new badge for client.\n");
        return 1;
    }
    memset((void *)client_reg_ptr, 0, sizeof(ads_server_registry_entry_t));
    ads_server_registry_insert((ads_server_registry_entry_t *)client_reg_ptr);

    ((ads_server_registry_entry_t *)client_reg_ptr)->ads.vspace = vspace;
    /* Create a badged endpoint for the client to send messages to.
     * Use the address of the client_registry_entry as the badge.
     */
    cspacepath_t src, dest;
    vka_cspace_make_path(get_ads_component()->server_vka,
                         get_ads_component()->server_ep_obj.cptr, &src);
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(get_ads_component()->server_vka, &dest_cptr);
    vka_cspace_make_path(get_ads_component()->server_vka, dest_cptr, &dest);

    int error = vka_cnode_mint(&dest, &src, seL4_AllRights, client_reg_ptr);
    if (error)
    {
        printf(ADSSERVS "main: Failed to mint client badge %x.\n",
               client_reg_ptr);
        return 1;
    }
    printf(ADSSERVS "main: Forged a new ADS cap with badge value: %x\n", client_reg_ptr);

    *cap_ret = dest_cptr;
    return 0;
}