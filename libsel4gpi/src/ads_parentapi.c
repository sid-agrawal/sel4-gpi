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
#include <vka/object_capops.h>

#include <sel4gpi/ads_parentapi.h>
#include <sel4gpi/ads_server.h>

seL4_Error
ads_server_parent_spawn_thread(simple_t *parent_simple, vka_t *parent_vka,
                                  vspace_t *parent_vspace,
                                  uint8_t priority,
                                  seL4_CPtr *server_ep_cap)
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

    /* Allocate the Endpoint that the server will be listening on. */
    error = vka_alloc_endpoint(parent_vka, &get_ads_server()->server_ep_obj);
    if (error != 0) {
        ZF_LOGE(ADSSERVP"spawn_thread: failed to alloc endpoint, err=%d.",
                error);
        return error;
    }

    *server_ep_cap = get_ads_server()->server_ep_obj.cptr; 

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

    error = vka_mint_object(parent_vka, &get_ads_server()->server_ep_obj,
                            &get_ads_server()->_badged_server_ep_cspath,
                            seL4_AllRights,
                            get_ads_server()->parent_badge_value);
    if (error != 0) {
        ZF_LOGE(ADSSERVP"spawn_thread: Failed to mint badged Endpoint cap to "
                "server.\n"
                "\tParent cannot confirm Server thread successfully spawned.");
        goto out;
    }

    sel4utils_thread_config_t config = thread_config_default(parent_simple,
                                                             parent_cspace_cspath.root,
                                                             seL4_NilData,
                                                             get_ads_server()->server_ep_obj.cptr,
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
                                   (sel4utils_thread_entry_fn)&ads_server_main,
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
    seL4_SetMR(ADSMSGREG_FUNC, FUNC_SERVER_SPAWN_SYNC_REQ);
    tag = seL4_MessageInfo_new(0, 0, 0, ADSMSGREG_SPAWN_SYNC_REQ_END);
    tag = seL4_Call(get_ads_server()->_badged_server_ep_cspath.capPtr, tag);

    /* Did all go well with the server? */
    if (seL4_GetMR(ADSMSGREG_FUNC) != FUNC_SERVER_SPAWN_SYNC_ACK) {
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
           get_ads_server()->server_ep_obj.cptr);
    return 0;

out:
    printf("spawn_thread: Server ran into an error.\n");
    if (get_ads_server()->_badged_server_ep_cspath.capPtr != 0) {
        vka_cspace_free_path(parent_vka, get_ads_server()->_badged_server_ep_cspath);
    }

    vka_free_object(parent_vka, &get_ads_server()->server_ep_obj);
    return error;
}