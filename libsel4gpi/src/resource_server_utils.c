#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>
#include <vka/vka.h>
#include <vka/capops.h>
#include <vspace/vspace.h>

#include <sel4gpi/pd_utils.h>
#include <sel4gpi/resource_server_utils.h>

#define SERVER_UTILS "SERVER_UTILS"

// Could use the server's debug function instead
#if RESOURCE_SERVER_DEBUG
#define RESOURCE_SERVER_PRINTF(...)  \
    do                               \
    {                                \
        printf("%s ", SERVER_UTILS); \
        printf(__VA_ARGS__);         \
    } while (0);
#else
#define RESOURCE_SERVER_PRINTF(...)
#endif

#define CHECK_ERROR(error, msg)    \
    do                             \
    {                              \
        if (error != seL4_NoError) \
        {                          \
            ZF_LOGE(SERVER_UTILS   \
                    "%s: %s"       \
                    ", %d.",       \
                    __func__,      \
                    msg,           \
                    error);        \
            return error;          \
        }                          \
    } while (0);

#define CHECK_ERROR_GOTO(check, msg, loc) \
    do                                    \
    {                                     \
        if ((check) != seL4_NoError)      \
        {                                 \
            ZF_LOGE(SERVER_UTILS "%s"     \
                                 ", %d.", \
                    msg,                  \
                    check);               \
            error = -1;                   \
            goto loc;                     \
        }                                 \
    } while (0);

int start_resource_server_pd(vka_t *vka,
                             uint64_t rde_id,
                             seL4_CPtr rde_pd_cap,
                             char *image_name,
                             seL4_CPtr *server_ep,
                             seL4_CPtr *server_pd_cap,
                             uint64_t *resource_manager_id)
{
    int error;

    // Create a temporary endpoint for the parent to listen on
    vka_object_t ep_object = {0};
    error = vka_alloc_endpoint(vka, &ep_object);
    CHECK_ERROR(error, "failed to allocate endpoint");

    // Create a new PD
    pd_client_context_t pd_os_cap;
    error = pd_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_PD), vka, &pd_os_cap);
    CHECK_ERROR(error, "failed to create new pd");

    if (server_pd_cap)
    {
        *server_pd_cap = pd_os_cap.badged_server_ep_cspath.capPtr;
    }

    // Create a new ADS Cap, which will be in the context of a PD and image
    ads_client_context_t ads_os_cap;
    error = ads_component_client_connect(sel4gpi_get_rde(GPICAP_TYPE_ADS), vka, &ads_os_cap);
    CHECK_ERROR(error, "failed to create new ads");

    // Make a new AS, loads an image
    error = pd_client_load(&pd_os_cap, &ads_os_cap, image_name);
    CHECK_ERROR(error, "failed to load pd image");

    // Copy the parent ep to the new PD
    seL4_Word parent_ep_slot;
    error = pd_client_send_cap(&pd_os_cap, ep_object.cptr, &parent_ep_slot);
    CHECK_ERROR(error, "failed to send parent's ep cap to pd");

    // Copy the RDE to the new PD
    if (rde_pd_cap > 0)
    {
        RESOURCE_SERVER_PRINTF("SENDING RDE\n");
        error = pd_client_add_rde(&pd_os_cap, rde_pd_cap, rde_id);
        CHECK_ERROR(error, "failed to send rde to pd");
    }

    // Start it
    error = pd_client_start(&pd_os_cap, parent_ep_slot); // with this arg.
    CHECK_ERROR(error, "failed to start pd");

    // Wait for it to finish starting
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);

    // Alloc cap receive path
    cspacepath_t received_cap_path;
    error = vka_cspace_alloc_path(vka, &received_cap_path);
    CHECK_ERROR(error, "failed to alloc receive endpoint");

    seL4_SetCapReceivePath(received_cap_path.root,
                           received_cap_path.capPtr,
                           received_cap_path.capDepth);

    tag = seL4_Recv(ep_object.cptr, NULL);
    int n_caps = seL4_MessageInfo_get_extraCaps(tag);
    CHECK_ERROR(n_caps != 1, "message from server does not contain ep");
    *server_ep = received_cap_path.capPtr;

    if (resource_manager_id)
    {
        *resource_manager_id = seL4_GetMR(0);
    }

    // Cleanup temporary endpoint
    // (XXX) Arya: why does this free cause future allocs to break?
    // vka_free_object(vka, &ep_object);

    return 0;
}

int resource_server_start(resource_server_context_t *context,
                          gpi_cap_t server_type,
                          seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr),
                          seL4_CPtr parent_ep,
                          int (*init_fn)())
{
    seL4_Error error;

    context->resource_type = server_type;
    context->request_handler = request_handler;
    context->server_vka = NULL;
    context->mo_ep = sel4gpi_get_rde(GPICAP_TYPE_MO);
    context->ads_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_ads_cap();
    context->pd_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();
    context->parent_ep = parent_ep;
    context->init_fn = init_fn;

    printf("Ramdisk: ADS_CAP: %ld\n", (seL4_Word)context->ads_conn.badged_server_ep_cspath.capPtr);
    printf("Ramdisk: PD_CAP: %ld\n", (seL4_Word)context->pd_conn.badged_server_ep_cspath.capPtr);
    printf("Ramdisk: MO ep: %ld\n", (seL4_Word)context->mo_ep);

    /* Allocate the Endpoint that the server will be listening on. */
    error = pd_client_alloc_ep(&context->pd_conn, &context->server_ep);
    CHECK_ERROR(error, "Failed to allocate endpoint for resource server");
    RESOURCE_SERVER_PRINTF("Allocated server ep at %d\n", (int)context->server_ep);

    RESOURCE_SERVER_PRINTF("Going to main function\n");
    return resource_server_main((void *)context);
}

seL4_MessageInfo_t resource_server_recv(resource_server_context_t *context,
                                        seL4_Word *sender_badge_ptr)
{
    /** NOTE:

     * the reply param of api_recv(third param) is only used in the MCS kernel.
     **/

    return api_recv(context->server_ep,
                    sender_badge_ptr,
                    context->mcs_reply);
}

void resource_server_reply(resource_server_context_t *context,
                           seL4_MessageInfo_t tag)
{
    api_reply(context->mcs_reply, tag);
}

int resource_server_next_slot(resource_server_context_t *context,
                              seL4_CPtr *slot)
{
    if (context->server_vka != NULL)
    {
        return vka_cspace_alloc(context->server_vka, slot);
    }
    else
    {
        return pd_client_next_slot(&context->pd_conn, slot);
    }
    return vka_cspace_alloc(context->server_vka, slot);
}

int resource_server_free_slot(resource_server_context_t *context,
                              seL4_CPtr slot)
{
    if (context->server_vka != NULL)
    {
        // First try to delete slot contents,
        // ignore error if slot is already empty
        cspacepath_t path;
        vka_cspace_make_path(context->server_vka, slot, &path);
        vka_cnode_delete(&path);
        vka_cspace_free(context->server_vka, slot);
    }
    else
    {
        return pd_client_free_slot(&context->pd_conn, slot);
    }
}

int resource_server_main(void *context_v)
{
    resource_server_context_t *context = (resource_server_context_t *)context_v;
    seL4_MessageInfo_t tag;
    seL4_Error error = 0;
    seL4_Word sender_badge;
    cspacepath_t received_cap_path;
    received_cap_path.root = PD_CAP_ROOT;
    received_cap_path.capDepth = PD_CAP_DEPTH;

    // Register the resource server with the PD component
    RESOURCE_SERVER_PRINTF("Registering the resource server with the PD component\n");
    error = pd_client_register_resource_manager(&context->pd_conn, context->resource_type, context->server_ep, &context->server_id);
    CHECK_ERROR_GOTO(error, "failed to register resource server", exit_main);
    RESOURCE_SERVER_PRINTF("Registered resource server, ID is 0x%lx\n", context->server_id);

    // Perform any server-specific initialization
    if (context->init_fn != NULL)
    {
        RESOURCE_SERVER_PRINTF("Calling server's init function\n");

        error = context->init_fn();
        CHECK_ERROR_GOTO(error, "failed to initialize resource server", exit_main);
    }

    // Allocate the first cap receive slot
    error = resource_server_next_slot(context, &received_cap_path.capPtr);
    CHECK_ERROR_GOTO(error, "failed to alloc cap receive slot", exit_main);

    seL4_SetCapReceivePath(
        received_cap_path.root,
        received_cap_path.capPtr,
        received_cap_path.capDepth);

    // Send our ep to the parent process
    // (XXX) Arya: We should not send out an unbadged copy of the endpoint
    // In the future, replace this with a new RDE mechanism?
    RESOURCE_SERVER_PRINTF("Messaging parent process at slot %d, sending ep %d\n", (int)context->parent_ep, (int)context->server_ep);
    tag = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetCap(0, context->server_ep);
    seL4_SetMR(0, context->server_id);
    seL4_Send(context->parent_ep, tag);

    while (1)
    {
        /* Receive a message */
        RESOURCE_SERVER_PRINTF("Ready to receive a message\n");
        tag = resource_server_recv(context, &sender_badge);
        int op = seL4_GetMR(RSMSGREG_FUNC);
        RESOURCE_SERVER_PRINTF("Received message, op is %d, passing to request handler\n", op);
        seL4_MessageInfo_t reply_tag = context->request_handler(tag, sender_badge, received_cap_path.capPtr);

        /**
         * Free slot and reallocate unless if this is an RR request
         * RR requests come from the root task, so we cannot message it
         * */
        if (op != RS_FUNC_GET_RR_REQ)
        {
            // We need to finish setting up for the next request before responding to this one
            // This is because the next request could be GET_RR, in which case we cannot
            // be requesting a new slot after it is sent

            // Save state of message registers for reply
            seL4_Word arg0 = seL4_GetMR(0);
            seL4_Word arg1 = seL4_GetMR(1);

            RESOURCE_SERVER_PRINTF("Freeing cap receive slot\n");
            error = resource_server_free_slot(context, received_cap_path.capPtr);
            CHECK_ERROR_GOTO(error, "failed to free cap receive slot", exit_main);

            // (XXX) Arya: Do we need to reallocate if we just delete the contents?
            RESOURCE_SERVER_PRINTF("Allocating new cap receive slot\n");
            error = resource_server_next_slot(context, &received_cap_path.capPtr);
            CHECK_ERROR_GOTO(error, "failed to alloc cap receive slot", exit_main);

            seL4_SetCapReceivePath(
                received_cap_path.root,
                received_cap_path.capPtr,
                received_cap_path.capDepth);

            // Restore state of message registers for reply
            seL4_SetMR(0, arg0);
            seL4_SetMR(1, arg1);
        }

        /* Reply to message */
        resource_server_reply(context, reply_tag);
    }

exit_main:
    RESOURCE_SERVER_PRINTF("Suspending resource server");
    return -1;
}

/**
 * Attach a MO from a client request to the server's ADS
 * @param mo_cap The MO cap to attach
 * @param vaddr Returns the vaddr where MO was attached
 */
int resource_server_attach_mo(resource_server_context_t *context,
                              seL4_CPtr mo_cap,
                              void **vaddr)
{
    // (XXX) Arya: Track the MO so we can unattach and free it later
    int error = 0;
    mo_client_context_t mo_conn;

    CHECK_ERROR(mo_cap == 0, "client did not attach MO for read/write op");

    mo_conn.badged_server_ep_cspath.capPtr = mo_cap;
    error = ads_client_attach(&context->ads_conn,
                              NULL,
                              &mo_conn,
                              vaddr);
    CHECK_ERROR(error, "failed to attach client's MO to ADS");

    return error;
}

int resource_server_get_rr(seL4_CPtr server_ep,
                           seL4_Word res_id,
                           void *remote_vaddr,
                           void *local_vaddr,
                           size_t size,
                           rr_state_t **ret_rr_state)
{
    RESOURCE_SERVER_PRINTF("requesting resource relations for ID 0x%lx\n", res_id);
    RESOURCE_SERVER_PRINTF("Shared mem local addr: %p, remote addr: %p\n", local_vaddr, remote_vaddr);

    // Send IPC to resource server
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, RSMSGREG_EXTRACT_RR_REQ_END);
    seL4_SetMR(RSMSGREG_FUNC, RS_FUNC_GET_RR_REQ);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_VADDR, (seL4_Word)remote_vaddr);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_SIZE, size);
    seL4_SetMR(RSMSGREG_EXTRACT_RR_REQ_ID, res_id);
    tag = seL4_Call(server_ep, tag);

    // Adjust rr state's row pointer if successful
    int result = seL4_MessageInfo_get_label(tag);
    if (result == seL4_NoError)
    {
        rr_state_t *rr_state = (rr_state_t *)local_vaddr;
        rr_state->csv_rows = (csv_rr_row_t *)(local_vaddr + sizeof(rr_state_t));
        *ret_rr_state = rr_state;
    }

    return result;
}

int resource_server_create_resource(resource_server_context_t *context,
                                    uint64_t resource_id)
{
    int error;

    RESOURCE_SERVER_PRINTF("Creating resource with ID 0x%lx\n", resource_id);

    error = pd_client_create_resource(&context->pd_conn,
                                      context->server_id,
                                      resource_id);

    return error;
}

int resource_server_give_resource(resource_server_context_t *context,
                                  uint64_t resource_id,
                                  uint64_t client_id,
                                  seL4_CPtr *dest)
{
    int error;

    RESOURCE_SERVER_PRINTF("Giving resource to client, resource ID 0x%lx, client ID 0x%lx\n", resource_id, client_id);

    error = pd_client_give_resource(&context->pd_conn,
                                    context->server_id,
                                    client_id,
                                    resource_id,
                                    dest);

    return error;
}