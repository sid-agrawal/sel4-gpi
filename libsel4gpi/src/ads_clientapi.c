/**
 * @file ads_clientapi.c
 * @author Sid Agrawal(sid@sid-agrawal.ca)
 * @brief Implements the ads client API from ads_client.h.
 * @version 0.1
 * @date 2022-04-05
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include<sel4gpi/ads_clientapi.h>

int ads_server_client_connect(seL4_CPtr server_ep_cap,
                              vka_t *client_vka,
                              ads_client_context_t *ret_conn){

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
    
    printf(ADSSERVC"%s %d ads_endpoint is %d: ", __FUNCTION__, __LINE__, server_ep_cap);
    debug_cap_identify(server_ep_cap);

    printf(ADSSERVC"Client: Set a receive path for the badged ep: %d\n", path.capPtr);
    seL4_SetMR(ADSMSGREG_FUNC, FUNC_CONNECT_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                               ADSMSGREG_CONNECT_REQ_END);

    tag = seL4_Call(server_ep_cap,tag);
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);

    ret_conn->badged_server_ep_cspath = path;;

    printf(ADSSERVC"Client: received badged endpoint and it was kept in %d:", path.capPtr);
    debug_cap_identify(path.capPtr);
    return 0;
}


int ads_client_attach(ads_client_context_t *conn, void* vaddr, size_t size, seL4_CPtr frame_cap)
{ 
    seL4_SetMR(ADSMSGREG_FUNC, FUNC_ATTACH_REQ);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  ADSMSGREG_ATTACH_REQ_END);

    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);

    return 0;
}

int ads_client_clone(ads_client_context_t *conn, vka_t* vka, void* omit_vaddr, ads_client_context_t *ret_conn)
{ 
    // Alloc a slot for the incoming cap.
    seL4_CPtr dest_cptr;
    vka_cspace_alloc(vka, &dest_cptr);
    cspacepath_t path;
    vka_cspace_make_path(vka, dest_cptr, &ret_conn->badged_server_ep_cspath);
    seL4_SetCapReceivePath(
        /* _service */      ret_conn->badged_server_ep_cspath.root,
        /* index */         ret_conn->badged_server_ep_cspath.capPtr,
        /* depth */         ret_conn->badged_server_ep_cspath.capDepth
    );

    seL4_SetMR(ADSMSGREG_FUNC, FUNC_CLONE_REQ);
    seL4_SetMR(ADSMSGREG_CLONE_REQ_OMIT_VA, (uintptr_t)omit_vaddr);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0,
                                                  ADSMSGREG_CLONE_REQ_END);

    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);
    return 0;
}

int ads_client_rm(ads_client_context_t *conn, void* vaddr, size_t size){
    return 0;
}

int ads_client_bind_cpu(ads_client_context_t *conn, seL4_CPtr cpu_cap) {
    return 0;
}