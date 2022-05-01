/**
 * @file ads_parentapi.c    
 * @author Sid Agrawal(sid@sid-agrawal.c)
 * @brief Implements functions needed by a parent to interact with the ads server from ads_parentapi.h
 * @version 0.1
 * @date 2022-04-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sel4/sel4.h>
#include <sel4utils/strerror.h>
#include <vka/vka.h>
#include <vka/object.h>
#include <vka/capops.h>

#include <sel4gpi/ads_parentapi.h>
#include <sel4gpi/ads_server.h>

void gpi_server_main()
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
        seL4_TCB_Suspend(get_ads_server()->server_thread.tcb.cptr);
    }


    printf(ADSSERVS"main: Entering main loop and accepting requests.\n");
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
        printf(ADSSERVS "main: Got message from %x\n", sender_badge);

        func = seL4_GetMR(ADSMSGREG_FUNC);

        // if the badge is not set, then it has to be a new connection request.
        if (sender_badge == ADS_SERVER_BADGE_VALUE_EMPTY && func != ADS_FUNC_CONNECT_REQ){
            printf(ADSSERVS "main: Badge not set, but not a connect request.\n");
            continue;
        }


        uint8_t cap_type = get_cap_type_from_badge(sender_badge); 
        /* Post */
        switch (cap_type) {
            GPICAP_TYPE_ADS:
                handle_ads_request();
                break;
                GPICAP_TYPE_CPU:
                handle_cpu_request();
                    break;
        }
    }

    //serial_server_func_kill();
    /* After we break out of the loop, seL4_TCB_Suspend ourselves */
    ZF_LOGI(ADSSERVS"main: Suspending.");
    seL4_TCB_Suspend(get_ads_server()->server_thread.tcb.cptr);
}
seL4_Error
gpi_server_parent_spawn_thread(simple_t *parent_simple, vka_t *parent_vka,
                               vspace_t *parent_vspace,
                               uint8_t priority,
                               seL4_CPtr server_endpoint)
{
    seL4_Error error;
    cspacepath_t parent_cspace_cspath;
    seL4_MessageInfo_t tag;

    if (parent_simple == NULL || parent_vka == NULL || parent_vspace == NULL) {
        return seL4_InvalidArgument;
    }

    memset(get_ads_server(), 0, sizeof(ads_server_context_t));

    /* Get a CPtr to the parent's root cnode. */
    vka_cspace_make_path(parent_vka, 0, &parent_cspace_cspath);

    get_ads_server()->server_vka = parent_vka;
    get_ads_server()->server_vspace = parent_vspace;
    get_ads_server()->server_cspace = parent_cspace_cspath.root;
    get_ads_server()->server_simple = parent_simple;
    get_ads_server()->server_endpoint = server_endpoint;


    /* And also allocate a badged copy of the Server's endpoint that the Parent
     * can use to send to the Server. This is used to allow the Server to report
     * back to the Parent on whether or not the Server successfully bound to a
     * platform serial driver.
     *
     * This badged endpoint will be reused by the library as the Parent's badged
     * Endpoint cap, if the Parent itself ever chooses to connect() to the
     * Server later on.
     */

    get_ads_server()->parent_badge_value = ADS_SERVER_BADGE_PARENT_VALUE;

    // error = vka_mint_object(parent_vka, get_ads_server()->server_endpoint,
    //                         &get_ads_server()->_badged_server_ep_cspath,
    //                         seL4_AllRights,
    //                         get_ads_server()->parent_badge_value);
    

    cspacepath_t src, dst;
    vka_cspace_make_path(get_ads_server()->server_vka,
                         get_ads_server()->server_endpoint, &src);
    vka_cspace_alloc_path(get_ads_server()->server_vka, &dst);
    error = vka_cnode_mint(&dst, &src, seL4_AllRights, get_ads_server()->parent_badge_value);
    if (error != 0) {
        ZF_LOGE(ADSSERVP"spawn_thread: Failed to mint badged Endpoint cap to "
                "server.\n"
                "\tParent cannot confirm Server thread successfully spawned.");
        goto out;
    }

    sel4utils_thread_config_t config = thread_config_default(parent_simple,
                                                             parent_cspace_cspath.root,
                                                             seL4_NilData,
                                                             seL4_CapNull,
                                                             priority);
    error = sel4utils_configure_thread_config(parent_vka,
                                              parent_vspace,
                                              parent_vspace,
                                              config,
                                              &get_ads_server()->server_thread);
    if (error != 0) {
        ZF_LOGE(ADSSERVP"spawn_thread: sel4utils_configure_thread failed "
                "with %d.", error);
        goto out;
    }

    NAME_THREAD(get_ads_server()->server_thread.tcb.cptr, "ads server");
    error = sel4utils_start_thread(&get_ads_server()->server_thread,
                                   (sel4utils_thread_entry_fn)&gpi_server_main,
                                   NULL, NULL, 1);
    if (error != 0) {
        ZF_LOGE(ADSSERVP"spawn_thread: sel4utils_start_thread failed with "
                "%d.", error);
        goto out;
    }

    /* When the Server is spawned, it will reply to tell us whether or not it
     * successfully bound itself to the platform serial device. Block here
     * and wait for that reply.
     */
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_SERVER_SPAWN_SYNC_REQ);
    tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_SPAWN_SYNC_REQ_END);
    tag = seL4_Call(get_ads_server()->_badged_server_ep_cspath.capPtr, tag);

    /* Did all go well with the server? */
    if (seL4_GetMR(ADSMSGREG_FUNC) != ADS_FUNC_SERVER_SPAWN_SYNC_ACK) {
        ZF_LOGE(ADSSERVP"spawn_thread: Server thread sync message after spawn "
                "was not a SYNC_ACK as expected.");
        error = seL4_InvalidArgument;
        goto out;
    }
    error = seL4_MessageInfo_get_label(tag);
    if (error != 0) {
        ZF_LOGE(ADSSERVP"spawn_thread: Server thread failed to bind to the "
                "platform serial device.");
        goto out;
    }

    printf(ADSSERVP"spawn_thread: Server thread binded well. at public EP %d\n",
           get_ads_server()->server_endpoint);
    return 0;

out:
    printf("spawn_thread: Server ran into an error.\n");
    if (get_ads_server()->_badged_server_ep_cspath.capPtr != 0) {
        vka_cspace_free_path(parent_vka, get_ads_server()->_badged_server_ep_cspath);
    }

    return error;
}