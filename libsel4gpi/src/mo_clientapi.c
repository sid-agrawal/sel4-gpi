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

int mo_component_client_connect(seL4_CPtr server_ep_cap,
                                seL4_CPtr free_slot,
                                seL4_Word num_pages,
                                mo_client_context_t *ret_conn)
{

    /* Send a REQ message to the server on its public EP */

    // Alloc a slot for the incoming cap.
    // seL4_CPtr dest_cptr;
    // vka_cspace_alloc(client_vka, &dest_cptr);
    // cspacepath_t path;
    // vka_cspace_make_path(client_vka, dest_cptr, &path);

    seL4_SetCapReceivePath(SEL4UTILS_CNODE_SLOT, /* Position of the cap to the CNODE */
                           free_slot,            /* CPTR in this CSPACE*/
                           /* This works coz we have a single level cnode with no guard.*/
                           seL4_WordBits); /* Depth i.e. how many bits of free_slot to interpret*/

    OSDB_PRINTF(MOSERVC "--%s %d mo_endpoint is %lu:__ \n",
                __FUNCTION__,
                __LINE__,
                server_ep_cap);
    // debug_cap_identify(MOSERVC, server_ep_cap);

    OSDB_PRINTF(MOSERVC "Set a receive path for the badged ep: %lu\n", free_slot);
    /* Set request type */
    OSDB_PRINTF(MOSERVC "Sending connect request for %lu pages\n", num_pages);
    seL4_SetMR(0, GPICAP_TYPE_MO);
    seL4_SetMR(MOMSGREG_CONNECT_REQ_NUM_PAGES, num_pages);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, MOMSGREG_CONNECT_REQ_END);

    tag = seL4_Call(server_ep_cap, tag);
    // assert(seL4_MessageInfo_get_extraCaps(tag) == 1);
    assert(seL4_MessageInfo_ptr_get_label(&tag) == 0);

    ret_conn->badged_server_ep_cspath.capPtr = free_slot;

    OSDB_PRINTF(MOSERVC "received badged endpoint and it was kept in %lu:__\n",
                free_slot);

    // debug_cap_identify(MOSERVC, path.capPtr);
    return 0;
}
