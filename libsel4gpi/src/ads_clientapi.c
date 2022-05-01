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

#include <vka/vka.h>
#include <vka/capops.h>

#include<sel4gpi/ads_clientapi.h>
#include<sel4gpi/badge_usage.h>

int ads_component_client_connect(seL4_CPtr server_ep_cap,
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
    
    printf(ADSSERVC"gpi endpoint is %d:", server_ep_cap);
    debug_cap_identify(ADSSERVC, server_ep_cap);

    printf(ADSSERVC"Set a receive path for the badged ep: %d\n", path.capPtr);

    /* Set request type */
    seL4_SetMR(0, GPICAP_TYPE_ADS);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);

    tag = seL4_Call(server_ep_cap,tag);
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);

    ret_conn->badged_server_ep_cspath = path;
    printf(ADSSERVC"Received badged endpoint and it was kept in:");
    debug_cap_identify(ADSSERVC, ret_conn->badged_server_ep_cspath.capPtr);
    return 0;
}


int ads_client_attach(ads_client_context_t *conn, void* vaddr, size_t size, seL4_CPtr frame_cap)
{ 
    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_ATTACH_REQ);
    seL4_SetMR(ADSMSGREG_ATTACH_REQ_VA, (seL4_Word) vaddr);
    seL4_SetMR(ADSMSGREG_ATTACH_REQ_SZ, (seL4_Word) size);
    seL4_SetCap(0, frame_cap);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1,
                                                  ADSMSGREG_ATTACH_REQ_END);

    printf(ADSSERVC "Sending attach request to server via EP: %d.\n",
           conn->badged_server_ep_cspath.capPtr);
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

    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_CLONE_REQ);
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

int ads_client_testing(ads_client_context_t *conn, vka_t *vka, 
                       ads_client_context_t *clone1,
                       ads_client_context_t *clone2,
                       ads_client_context_t *clone3) {

    int error = 0;
    cspacepath_t rand_cap_orig_path, rand_cap_badged_path;

    seL4_CPtr rand_cap = vka_alloc_endpoint_leaky(vka);
    vka_cspace_make_path(vka, rand_cap, &rand_cap_orig_path);
    assert(error == 0);
    
    error = vka_cspace_alloc_path(vka, &rand_cap_badged_path);
    assert(error == 0);
    error = vka_cnode_mint(&rand_cap_badged_path,
                               &rand_cap_orig_path,
                               seL4_AllRights,
                               0xdeedbeef);
    assert(error == 0);

    seL4_SetMR(ADSMSGREG_FUNC, ADS_FUNC_TESTING_REQ);
    seL4_SetCap(0, clone1->badged_server_ep_cspath.capPtr);
    seL4_SetCap(1, rand_cap_badged_path.capPtr);
    //seL4_SetCap(1, clone2->badged_server_ep_cspath.capPtr);

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 2,
                                                  ADSMSGREG_TESTING_REQ_END);

    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    return 0;
                       }