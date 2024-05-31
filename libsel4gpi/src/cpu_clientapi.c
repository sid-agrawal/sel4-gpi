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
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/gpi_client.h>

// Defined for utility printing macros
#define DEBUG_ID CPU_DEBUG
#define SERVER_ID CPUSERVC

int cpu_component_client_connect(seL4_CPtr server_ep_cap,
                                 seL4_CPtr free_slot,
                                 cpu_client_context_t *ret_conn)
{

    /* Send a REQ message to the server on its public EP */
    seL4_SetCapReceivePath(SEL4UTILS_CNODE_SLOT, /* Position of the cap to the CNODE */
                           free_slot,            /* CPTR in this CSPACE */
                           /* This works coz we have a single level cnode with no guard.*/
                           seL4_WordBits); /* Depth i.e. how many bits of free_slot to interpret*/

    OSDB_PRINTF("Set a receive path for the badged ep: %lu\n", free_slot);

    /* Set request type */
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CONNECT_REQ);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, CPUMSGREG_CONNECT_REQ_END);
    tag = seL4_Call(server_ep_cap, tag);

    ret_conn->badged_server_ep_cspath.capPtr = free_slot;

    OSDB_PRINTF("received badged endpoint and it was kept in %lu:__\n", free_slot);
    
    return seL4_MessageInfo_ptr_get_label(&tag);
}

int cpu_client_config(cpu_client_context_t *cpu,
                      ads_client_context_t *ads,
                      pd_client_context_t *pd,
                      mo_client_context_t *ipc_buf_mo,
                      seL4_Word cnode_guard,
                      seL4_CPtr fault_ep_position,
                      seL4_Word ipc_buf_addr)
{
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CONFIG_REQ);
    seL4_SetMR(CPUMSGREG_CONFIG_IPC_BUF_ADDR, ipc_buf_addr);
    seL4_SetMR(CPUMSGREG_CONFIG_FAULT_EP, fault_ep_position);
    seL4_SetMR(CPUMSGREG_CONFIG_CNODE_GUARD, cnode_guard);

    /* Send the badged endpoint cap of the ads client as a cap */
    seL4_Uint64 extraCaps = 2;
    seL4_SetCap(0, pd->badged_server_ep_cspath.capPtr);       /*cspace*/
    seL4_SetCap(1, ads->badged_server_ep_cspath.capPtr);      /*vspace*/
    if (ipc_buf_mo && ipc_buf_mo->badged_server_ep_cspath.capPtr != 0)
    {
        seL4_SetCap(2, ipc_buf_mo->badged_server_ep_cspath.capPtr); /* ipc buffer */
        extraCaps = 3;
    }

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, extraCaps,
                                                  CPUMSGREG_CONFIG_REQ_END);

    tag = seL4_Call(cpu->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int cpu_client_change_vspace(cpu_client_context_t *conn,
                             ads_client_context_t *ads_conn)
{
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_CHANGE_VSPACE_REQ);

    /* Send the badged endpoint cap of the ads client as a cap */
    seL4_SetCap(0, ads_conn->badged_server_ep_cspath.capPtr); /*vspace*/

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  CPUMSGREG_CHANGE_VSPACE_REQ_END);

    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int cpu_client_start(cpu_client_context_t *conn)
{
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_START_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  CPUMSGREG_START_REQ_END);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int cpu_client_set_tls_base(cpu_client_context_t *cpu, void *tls_base)
{
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_SET_TLS_REQ);
    seL4_SetMR(CPUMSGREG_SET_TLS_REQ_BASE, (seL4_Word)tls_base);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  CPUMSGREG_SET_TLS_REQ_END);
    tag = seL4_Call(cpu->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}

int cpu_client_suspend(cpu_client_context_t *cpu)
{
    seL4_SetMR(CPUMSGREG_FUNC, CPU_FUNC_SUSPEND_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  CPUMSGREG_SUSPEND_REQ_END);
    tag = seL4_Call(cpu->badged_server_ep_cspath.capPtr, tag);

    return seL4_MessageInfo_ptr_get_label(&tag);
}
