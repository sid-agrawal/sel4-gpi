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

#include<sel4gpi/mo_clientapi.h>
#include<sel4gpi/ads_clientapi.h>
#include<sel4gpi/badge_usage.h>
#include<sel4gpi/debug.h>

int mo_component_client_connect(seL4_CPtr server_ep_cap,
                              vka_t *client_vka,
                              uint32_t num_pages,
                              mo_client_context_t *ret_conn){

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

    OSDB_PRINTF(MOSERVC"%s %d mo_endpoint is %lu:__ \n", __FUNCTION__, __LINE__, server_ep_cap);
    // debug_cap_identify(MOSERVC, server_ep_cap);

    OSDB_PRINTF(MOSERVC"Set a receive path for the badged ep: %lu\n", path.capPtr);
    /* Set request type */
    seL4_SetMR(0, GPICAP_TYPE_MO);
    seL4_SetMR(MOMSGREG_CONNECT_REQ_NUM_PAGES, num_pages);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, MOMSGREG_CONNECT_REQ_END );

    tag = seL4_Call(server_ep_cap,tag);
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);

    ret_conn->badged_server_ep_cspath = path;;

    OSDB_PRINTF(MOSERVC"received badged endpoint and it was kept in %lu:__\n", path.capPtr);
    // debug_cap_identify(MOSERVC, path.capPtr);
    return 0;
}
