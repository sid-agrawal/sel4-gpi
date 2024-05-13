/**
 * @file mo_clientapi.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the mo client API from moclient.h.
 * @version 0.1
 * @date 2024-01-07
 *
 * @copyright Copyright (c) 2024
 *
 */

#include <sel4gpi/mo_clientapi.h>
#include <sel4gpi/ads_clientapi.h>
#include <sel4gpi/badge_usage.h>
#include <sel4gpi/debug.h>
#include <sel4gpi/gpi_client.h>

// Defined for utility printing macros
#define DEBUG_ID MO_DEBUG
#define SERVER_ID MOSERVS

int mo_component_client_connect(seL4_CPtr server_ep_cap,
                                seL4_CPtr free_slot,
                                seL4_Word num_pages,
                                mo_client_context_t *ret_conn)
{
    /* Send a REQ message to the server on its public EP */
    seL4_SetCapReceivePath(SEL4UTILS_CNODE_SLOT, /* Position of the cap to the CNODE */
                           free_slot,            /* CPTR in this CSPACE */
                           /* This works coz we have a single level cnode with no guard.*/
                           seL4_WordBits); /* Depth i.e. how many bits of free_slot to interpret*/

    seL4_SetMR(0, GPICAP_TYPE_MO);
    seL4_SetMR(MOMSGREG_CONNECT_REQ_NUM_PAGES, num_pages);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, MOMSGREG_CONNECT_REQ_END);
    tag = seL4_Call(server_ep_cap, tag);

    ret_conn->badged_server_ep_cspath.capPtr = free_slot;
    ret_conn->id = seL4_GetMR(MOMSGREG_CONNECT_ACK_ID);

    OSDB_PRINTF("received badged endpoint and it was kept in %lu:__\n",
                free_slot);

    return seL4_MessageInfo_ptr_get_label(&tag);
}
