/**
 * @file counter_clientapi.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the counter client API from counter_client.h.
 * @version 0.1
 * @date 2022-04-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include<sel4gpi/counter_clientapi.h>

int counter_server_client_connect(seL4_CPtr server_ep_cap,
                                vka_t *client_vka,
                                counter_client_context_t *ret_conn){

    /* Send a REQ message to the server on its public EP */

    // Alloc a slot for the incoming cap.
    seL4_CPtr dest_cptr;
    int error;
    error = vka_cspace_alloc(client_vka, &dest_cptr);
    if (error) {
        printf(COUNTERSERVC"Failed to allocate slot for the counter cap");
        return error;
    }
    printf(COUNTERSERVC"%s:%d allocated slot=%d for the counter cap\n", __FILE__, __LINE__);
    cspacepath_t path;
    vka_cspace_make_path(client_vka, dest_cptr, &path);
    if (error) {
        printf(COUNTERSERVC"Failed to allocate slot for the counter cap");
        return error;
    }
    seL4_SetCapReceivePath(
        /* _service */      path.root,
        /* index */         path.capPtr,
        /* depth */         path.capDepth
    );
    
    printf(COUNTERSERVC"%s:%d counter_endpoint is %d: ", __FUNCTION__, __LINE__, server_ep_cap);
    debug_cap_identify(server_ep_cap);

    printf(COUNTERSERVC"Client: Set a receive path for the badged ep: %d\n", path.capPtr);
    seL4_SetMR(CSMSGREG_FUNC, FUNC_CONNECT_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                               CSMSGREG_CONNECT_REQ_END);

    tag = seL4_Call(server_ep_cap,tag);
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);

    ret_conn->badged_server_ep_cspath = path;;

    printf(COUNTERSERVC"Client: received badged endpoint and it was kept in %d\n",
    path.capPtr);
    debug_cap_identify(path.capPtr);
    return 0;
}


int counter_client_increment(counter_client_context_t *conn) {

    seL4_SetMR(CSMSGREG_FUNC, FUNC_INCREMENT_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  CSMSGREG_INCREMENT_REQ_END);

    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return 0;
}