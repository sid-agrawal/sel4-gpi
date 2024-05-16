#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sel4/sel4.h>
#include <sel4utils/process.h>

#include <vka/vka.h>
#include <vka/object.h>
#include <vka/capops.h>

#include <sel4gpi/pd_utils.h>
#include <sel4gpi/resource_server_utils.h>
#include <sel4gpi/resource_server_remote_utils.h>

#define CHECK_ERROR(error, msg)    \
    do                             \
    {                              \
        if (error != seL4_NoError) \
        {                          \
            ZF_LOGE("%s"           \
                    ", %d.",       \
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
            ZF_LOGE(SERVER_UTILS ": %s"     \
                                 ", %d.", \
                    msg,                  \
                    check);               \
            error = -1;                   \
            goto loc;                     \
        }                                 \
    } while (0);

int resource_server_start(resource_server_context_t *context,
                          gpi_cap_t server_type,
                          seL4_MessageInfo_t (*request_handler)(seL4_MessageInfo_t, seL4_Word, seL4_CPtr, bool *),
                          seL4_CPtr parent_ep,
                          int (*init_fn)())
{
    seL4_Error error;

    context->resource_type = server_type;
    context->request_handler = request_handler;
    context->mo_ep = sel4gpi_get_rde(GPICAP_TYPE_MO);
    context->resspc_ep = sel4gpi_get_rde(GPICAP_TYPE_RESSPC);
    context->ads_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_rde_by_ns_id(sel4gpi_get_binded_ads_id(), GPICAP_TYPE_ADS);
    context->pd_conn.badged_server_ep_cspath.capPtr = sel4gpi_get_pd_cap();
    context->parent_ep = parent_ep;
    context->init_fn = init_fn;

    printf("Resource server ADS_CAP: %ld\n", (seL4_Word)context->ads_conn.badged_server_ep_cspath.capPtr);
    printf("Resource server PD_CAP: %ld\n", (seL4_Word)context->pd_conn.badged_server_ep_cspath.capPtr);
    printf("Resource server MO ep: %ld\n", (seL4_Word)context->mo_ep);
    printf("Resource server RESSPC ep: %ld\n", (seL4_Word)context->resspc_ep);

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
    return pd_client_next_slot(&context->pd_conn, slot);
}

int resource_server_free_slot(resource_server_context_t *context,
                              seL4_CPtr slot)
{
    return pd_client_free_slot(&context->pd_conn, slot);
}

int resource_server_main(void *context_v)
{
    resource_server_context_t *context = (resource_server_context_t *)context_v;
    seL4_MessageInfo_t tag;
    seL4_Error error = 0;
    seL4_Word sender_badge;
    cspacepath_t received_cap_path;
    bool need_new_receive_slot;
    received_cap_path.root = PD_CAP_ROOT;
    received_cap_path.capDepth = PD_CAP_DEPTH;

    // Register the resource server with the PD component
    // This is done by creating a default resource space
    seL4_CPtr free_slot;
    error = resource_server_next_slot(context, &free_slot);
    CHECK_ERROR_GOTO(error, "failed to get next slot", exit_main);
    error = resspc_client_connect(context->resspc_ep, free_slot, context->resource_type, context->server_ep, &context->default_space);
    CHECK_ERROR_GOTO(error, "failed to register default resource space for server", exit_main);
    RESOURCE_SERVER_PRINTF("Registered resource server, default space ID is 0x%lx\n", context->default_space.id);

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

    // Send our space ID to the parent process
    RESOURCE_SERVER_PRINTF("Messaging parent process at slot %d, sending space ID %d\n", (int)context->parent_ep, context->default_space.id);
    tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, context->default_space.id);
    seL4_Send(context->parent_ep, tag);

    while (1)
    {
        /* Receive a message */
        RESOURCE_SERVER_PRINTF("Ready to receive a message\n");
        tag = resource_server_recv(context, &sender_badge);
        int op = seL4_GetMR(RSMSGREG_FUNC);
        RESOURCE_SERVER_PRINTF("Received message, op is %d, passing to request handler\n", op);
        seL4_MessageInfo_t reply_tag = context->request_handler(tag, sender_badge, received_cap_path.capPtr, &need_new_receive_slot);

        /**
         * Free slot and reallocate only if needed
         * Some requests come from the root task, so we cannot message it
         * */
        if (need_new_receive_slot)
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
    int error = 0;
    mo_client_context_t mo_conn;

    CHECK_ERROR(mo_cap == 0, "client did not attach MO for read/write op");

    mo_conn.badged_server_ep_cspath.capPtr = mo_cap;
    error = ads_client_attach(&context->ads_conn,
                              NULL,
                              &mo_conn,
                              SEL4UTILS_RES_TYPE_GENERIC,
                              vaddr);
    CHECK_ERROR(error, "failed to attach client's MO to ADS");

    return error;
}

/**
 * Remove a previously attached MO from the server's ADS
 * @param vaddr The vaddr where MO was attached
 */
int resource_server_unattach(resource_server_context_t *context,
                             void *vaddr)
{
    int error = 0;

    CHECK_ERROR(vaddr == NULL, "cannot unattach a NULL vaddr");

    error = ads_client_rm(&context->ads_conn,
                          vaddr);
    CHECK_ERROR(error, "failed to unattach from ADS");

    return error;
}


int resource_server_create_resource(resource_server_context_t *context,
                                    uint64_t resource_id)
{
    int error;

    RESOURCE_SERVER_PRINTF("Creating resource with ID 0x%lx\n", resource_id);

    error = pd_client_create_resource(&context->pd_conn,
                                      context->default_space.id,
                                      resource_id);

    return error;
}

int resource_server_give_resource(resource_server_context_t *context,
                                  uint64_t ns_id,
                                  uint64_t resource_id,
                                  uint64_t client_id,
                                  seL4_CPtr *dest)
{
    int error;

    RESOURCE_SERVER_PRINTF("Giving resource to client, resource ID 0x%lx, client ID 0x%lx\n", resource_id, client_id);

    error = pd_client_give_resource(&context->pd_conn,
                                    context->default_space.id,
                                    ns_id,
                                    client_id,
                                    resource_id,
                                    dest);

    return error;
}

int resource_server_new_ns(resource_server_context_t *context,
                           uint64_t client_id,
                           uint64_t *ns_id)
{
    int error;

    RESOURCE_SERVER_PRINTF("Creating new NS\n");

    error = pd_client_register_namespace(&context->pd_conn, context->default_space.id, client_id, ns_id);

    return error;
}