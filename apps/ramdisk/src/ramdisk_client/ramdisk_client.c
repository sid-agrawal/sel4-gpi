#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vka/capops.h>
#include <vspace/vspace.h>

#include <sel4gpi/pd_clientapi.h>

#include <ramdisk_client.h>
#include <ramdisk_shared.h>

#define RAMDISK_C "RamDisk Client: "
#define RAMDISK_APP "ramdisk_server"

#if RAMDISK_DEBUG
#define RAMDISK_PRINTF(...)       \
    do                            \
    {                             \
        printf("%s ", RAMDISK_C); \
        printf(__VA_ARGS__);      \
    } while (0);
#else
#define RAMDISK_PRINTF(...)
#endif

#define CHECK_ERROR(check, msg)        \
    do                                 \
    {                                  \
        if ((check) != seL4_NoError)   \
        {                              \
            ZF_LOGE(RAMDISK_C "%s: %s" \
                              ", %d.", \
                    __func__,          \
                    msg,               \
                    error);            \
            error = -1;                \
            return error;              \
        }                              \
    } while (0);

static ramdisk_client_context_t ramdisk_client;

int start_ramdisk_pd(vka_t *vka,
                     seL4_CPtr gpi_ep,
                     seL4_CPtr *ramdisk_ep)
{
    int error;

    // Create an endpoint for the parent to listen on
    vka_object_t ep_object = {0};
    error = vka_alloc_endpoint(vka, &ep_object);
    CHECK_ERROR(error, "failed to allocate endpoint");

    // Create a new PD
    pd_client_context_t pd_os_cap;
    error = pd_component_client_connect(gpi_ep, vka, &pd_os_cap);
    CHECK_ERROR(error, "failed to create new pd");

    // Create a new ADS Cap, which will be in the context of a PD and image
    ads_client_context_t ads_os_cap;
    error = ads_component_client_connect(gpi_ep, vka, &ads_os_cap);
    CHECK_ERROR(error, "failed to create new ads");

    // Make a new AS, loads an image
    error = pd_client_load(&pd_os_cap, &ads_os_cap, RAMDISK_APP);
    CHECK_ERROR(error, "failed to load pd image");

    // Copy the parent ep to the new PD
    seL4_Word parent_ep_slot;
    error = pd_client_send_cap(&pd_os_cap, ep_object.cptr, &parent_ep_slot);
    CHECK_ERROR(error, "failed to send parent's ep cap to pd");

    // Start it
    error = pd_client_start(&pd_os_cap, parent_ep_slot); // with this arg.
    CHECK_ERROR(error, "failed to start pd");

    // Wait for it to finish starting
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);

    /* Alloc cap receive path*/
    cspacepath_t received_cap_path;
    error = vka_cspace_alloc_path(vka, &received_cap_path);
    CHECK_ERROR(error, "failed to alloc receive endpoint");

    seL4_SetCapReceivePath(received_cap_path.root,
                           received_cap_path.capPtr,
                           received_cap_path.capDepth);

    tag = seL4_Recv(ep_object.cptr, NULL);
    int n_caps = seL4_MessageInfo_get_extraCaps(tag);
    CHECK_ERROR(n_caps != 1, "message from ramdisk does not contain ep");
    *ramdisk_ep = received_cap_path.capPtr;

    RAMDISK_PRINTF("Successfully started ramdisk server\n");
    return 0;
}

int ramdisk_client_sanity_test(seL4_CPtr server_ep_cap,
                               mo_client_context_t *mo,
                               seL4_Word *res)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR(RAMDISK_MR_OP, RAMDISK_SANITY_TEST);
    seL4_SetCap(0, mo->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(server_ep_cap, tag);

    int error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "failed ramdisk sanity test\n");

    *res = seL4_GetMR(0);
    return 0;
}

int ramdisk_client_alloc_block(seL4_CPtr server_ep_cap,
                               vka_t *client_vka,
                               seL4_CPtr free_slot,
                               ramdisk_client_context_t *ret_conn)
{
    /* Send a request to the server on its public EP */

    cspacepath_t path;
    if (client_vka != NULL)
    {
        // Alloc a slot for the incoming cap.
        seL4_CPtr dest_cptr;
        vka_cspace_alloc(client_vka, &dest_cptr);
        vka_cspace_make_path(client_vka, dest_cptr, &path);
    }
    else
    {
        path.capDepth = PD_CAP_DEPTH;
        path.root = PD_CAP_ROOT;
        path.capPtr = free_slot;
    }

    seL4_SetCapReceivePath(
        /* _service */ path.root,
        /* index */ path.capPtr,
        /* depth */ path.capDepth);

    /* Request a new block from server */
    seL4_SetMR(RAMDISK_MR_OP, RAMDISK_GET_BLOCK);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);

    tag = seL4_Call(server_ep_cap, tag);
    int error = seL4_MessageInfo_get_label(tag);
    CHECK_ERROR(error, "failed to get block from ramdisk server\n");
    assert(seL4_MessageInfo_get_extraCaps(tag) == 1);

    ret_conn->badged_server_ep_cspath = path;
    return 0;
}

int ramdisk_client_read(ramdisk_client_context_t *conn, mo_client_context_t *mo)
{
    seL4_Error error;

    /* Send IPC to ramdisk server */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR(RAMDISK_MR_OP, RAMDISK_READ);
    seL4_SetCap(RAMDISK_CAP_MO, mo->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    error = seL4_MessageInfo_get_label(tag);

    return error;
}

int ramdisk_client_write(ramdisk_client_context_t *conn, mo_client_context_t *mo)
{
    seL4_Error error;

    /* Send IPC to ramdisk server */
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR(RAMDISK_MR_OP, RAMDISK_WRITE);
    seL4_SetCap(RAMDISK_CAP_MO, mo->badged_server_ep_cspath.capPtr);
    tag = seL4_Call(conn->badged_server_ep_cspath.capPtr, tag);
    error = seL4_MessageInfo_get_label(tag);

    return error;
}

uint64_t get_ramdisk_block_size()
{
    return RAMDISK_BLOCK_SIZE;
}