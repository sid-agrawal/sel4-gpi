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

#include <sel4gpi/cpu_clientapi.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>

int cpu_component_client_connect(seL4_CPtr server_ep_cap,
                                 seL4_CPtr free_slot,
                                 cpu_client_context_t *ret_conn)
{

    /* Send a REQ message to the server on its public EP */
    seL4_SetCapReceivePath(SEL4UTILS_CNODE_SLOT, /* Position of the cap to the CNODE */
                           free_slot,            /* CPTR in this CSPACE */
                           /* This works coz we have a single level cnode with no guard.*/
                           seL4_WordBits); /* Depth i.e. how many bits of free_slot to interpret*/

    OSDB_PRINTF(CPU_DEBUG, CPUSERVC "%s %d cpu_endpoint is %lu:__ \n", __FUNCTION__, __LINE__, server_ep_cap);
    // debug_cap_identify(CPUSERVC, server_ep_cap);

    OSDB_PRINTF(CPU_DEBUG, CPUSERVC "Set a receive path for the badged ep: %lu\n", free_slot);
    /* Set request type */
    seL4_SetMR(0, GPICAP_TYPE_CPU);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);

    tag = seL4_Call(server_ep_cap, tag);
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);

    ret_conn->badged_server_ep_cspath.capPtr = free_slot;

    OSDB_PRINTF(CPU_DEBUG, CPUSERVC "received badged endpoint and it was kept in %lu:__\n", free_slot);
    // debug_cap_identify(CPUSERVC, path.capPtr);
    return 0;
}

int cpu_client_config(cpu_client_context_t *conn,
                      ads_client_context_t *ads_conn,
                      mo_client_context_t *ipc_buf_mo,
                      seL4_CPtr cspace_root,
                      seL4_Word cnode_guard,
                      seL4_CPtr fault_ep_position,
                      seL4_Word ipc_buf_addr,
                      seL4_Word stack_addr)
{
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CONFIG_REQ);
    seL4_SetMR(CPUMSGREG_CONFIG_IPC_BUF_ADDR, ipc_buf_addr);
    seL4_SetMR(CPUMSGREG_CONFIG_STACK_ADDR, stack_addr);
    seL4_SetMR(CPUMSGREG_CONFIG_FAULT_EP, fault_ep_position);
    seL4_SetMR(CPUMSGREG_CONFIG_CNODE_GUARD, cnode_guard);

    /* Send the badged endpoint cap of the ads client as a cap */
    seL4_Uint64 extraCaps = 2;
    seL4_SetCap(0, cspace_root);                              /*cspace*/
    seL4_SetCap(1, ads_conn->badged_server_ep_cspath.capPtr); /*vspace*/
    if (ipc_buf_mo)
    {
        seL4_SetCap(2, ipc_buf_mo->badged_server_ep_cspath.capPtr); /* ipc buffer */
        extraCaps = 3;
    }

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, extraCaps,
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

    OSDB_PRINTF(CPU_DEBUG, CPUSERVC "INVOKING CAP %lu\n", conn->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    return 0;
}

int cpu_client_start(cpu_client_context_t *conn,
                     sel4utils_thread_entry_fn entry_fn,
                     seL4_Word initial_stack,
                     seL4_Word arg0)
{
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_START_REQ);
    seL4_SetMR(CPUMSGREG_START_FUNC_VADDR, (seL4_Word)entry_fn);
    seL4_SetMR(CPUMSGREG_START_INIT_STACK_ADDR, initial_stack);
    seL4_SetMR(CPUMSGREG_START_ARG0, arg0);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  CPUMSGREG_START_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);
    return 0;
}
