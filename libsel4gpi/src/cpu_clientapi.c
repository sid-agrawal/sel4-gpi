/**
 * @file cpu_clientapi.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the cpu client API from cpu_client.h.
 * @version 0.1
 * @date 2022-04-05
 *
 * @copyright Copyright (c) 2022
 *
 */

#include<sel4gpi/cpu_clientapi.h>
#include<sel4gpi/ads_clientapi.h>
#include<sel4gpi/badge_usage.h>
#include<sel4gpi/debug.h>

int cpu_component_client_connect(seL4_CPtr server_ep_cap,
                              vka_t *client_vka,
                              cpu_client_context_t *ret_conn){

    /* Send a REQ message to the server on its public EP */

    // Alloc a slot for the incoming cap.
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(client_vka, &dest_cptr);
    cspacepath_t path;
    vka_cspace_make_path(client_vka, dest_cptr, &path);
    seL4_SetCapReceivePath(
        /* _service */      path.root,
        /* index */         path.capPtr,
        /* depth */         path.capDepth
    );

    OSDB_PRINTF(CPUSERVC"%s %d cpu_endpoint is %lu:__ \n", __FUNCTION__, __LINE__, server_ep_cap);
    // debug_cap_identify(CPUSERVC, server_ep_cap);

    OSDB_PRINTF(CPUSERVC"Set a receive path for the badged ep: %lu\n", path.capPtr);
    /* Set request type */
    seL4_SetMR(0, GPICAP_TYPE_CPU);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);

    tag = seL4_Call(server_ep_cap,tag);
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);

    ret_conn->badged_server_ep_cspath = path;;

    OSDB_PRINTF(CPUSERVC"received badged endpoint and it was kept in %lu:__\n", path.capPtr);
    // debug_cap_identify(CPUSERVC, path.capPtr);
    return 0;
}

int cpu_client_config(cpu_client_context_t *conn,
                      ads_client_context_t *ads_conn,
                      seL4_CPtr cspace_root,
                      seL4_CPtr fault_ep_position)
{
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CONFIG_REQ);
    seL4_SetMR(CPUMSGREG_CONFIG_FAULT_EP, fault_ep_position);

    /* Send the badged endpoint cap of the ads client as a cap */
    seL4_SetCap(0, ads_conn->badged_server_ep_cspath.capPtr); /*vspace*/
    seL4_SetCap(1, cspace_root); /*cspace*/

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 2,
                                                  CPUMSGREG_CONFIG_REQ_END);

    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    return 0;
}

int cpu_client_change_vspace(cpu_client_context_t *conn,
                      ads_client_context_t *ads_conn)
{
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CHANGE_VSPACE_REQ);

    /* Send the badged endpoint cap of the ads client as a cap */
    seL4_SetCap(0, ads_conn->badged_server_ep_cspath.capPtr); /*vspace*/

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 2,
                                                  CPUMSGREG_CHANGE_VSPACE_REQ_END);

    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    return 0;
}

int cpu_client_start(cpu_client_context_t *conn,
                     sel4utils_thread_entry_fn entry_fn)
{
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_START_REQ);
    seL4_SetMR(CPUMSGREG_START_FUNC_VADDR, (seL4_Word)entry_fn);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  CPUMSGREG_START_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    return 0;
}
